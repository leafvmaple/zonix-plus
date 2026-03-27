#pragma once

#include <kernel/bootinfo.h>
#include <base/elf.h>

static void* memcpy(void* dst, const void* src, UINTN n) {
    UINT8* d = (UINT8*)dst;
    const UINT8* s = (const UINT8*)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

static void* memset(void* dst, int c, UINTN n) {
    UINT8* d = (UINT8*)dst;
    while (n--) {
        *d++ = (UINT8)c;
    }
    return dst;
}

static void uefi_print(EFI_SYSTEM_TABLE* st, const CHAR16* str) {
    if (st && st->ConOut) {
        st->ConOut->OutputString(st->ConOut, (CHAR16*)str);
    }
}

static void uefi_print_hex(EFI_SYSTEM_TABLE* st, UINT64 val) {
    CHAR16 buf[21];
    const CHAR16* hex = UEFI_STR(L"0123456789ABCDEF");
    buf[0] = L'0';
    buf[1] = L'x';
    for (int i = 0; i < 16; i++) {
        buf[17 - i] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[18] = L'\r';
    buf[19] = L'\n';
    buf[20] = L'\0';
    uefi_print(st, buf);
}

static int uefi_load_elf(void* elf_buffer, struct boot_info* bi, UINT64 kernel_virt_base) {
    ElfHdr* elf = (ElfHdr*)elf_buffer;
    if (elf->e_magic != ELF_MAGIC) {
        return -1;
    }

    UINT64 kstart = ~0ULL, kend = 0;
    ProgHdr* ph = (ProgHdr*)((UINT8*)elf + elf->e_phoff);
    ProgHdr* eph = ph + elf->e_phnum;

    for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PT_LOAD) {
            continue;
        }

        UINT64 phys = ph->p_pa;
        if (phys >= kernel_virt_base) {
            phys = ph->p_va - kernel_virt_base;
        }

        UINT8* dst = (UINT8*)(UINTN)phys;
        UINT8* src = (UINT8*)elf + ph->p_offset;

        if (phys < kstart) {
            kstart = phys;
        }
        if (phys + ph->p_memsz > kend) {
            kend = phys + ph->p_memsz;
        }

        memcpy(dst, src, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }
    }

    bi->kernel_start = (uint32_t)kstart;
    bi->kernel_end = (uint32_t)kend;
    bi->kernel_entry = (uint32_t)(elf->e_entry - kernel_virt_base);
    return 0;
}

static EFI_STATUS uefi_get_memory_map(EFI_BOOT_SERVICES* bs, struct boot_info* bi, UINT64 mmap_addr,
                                      UINTN mmap_max_entries, uint32_t mem_lower, UINT64 mem_upper_min) {
    UINTN map_key = 0;
    UINTN map_size = 0;
    UINTN desc_size = 0;
    UINT32 desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR* map = NULL;

    EFI_STATUS status = bs->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
    if (status != EFI_BUFFER_TOO_SMALL) {
        return status;
    }

    map_size += 2 * desc_size;
    status = bs->AllocatePool(EfiLoaderData, map_size, (VOID**)&map);
    if (EFI_ERROR(status)) {
        return status;
    }

    status = bs->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
    if (EFI_ERROR(status)) {
        bs->FreePool(map);
        return status;
    }

    UINTN num_desc = map_size / desc_size;
    bi->mmap_addr = mmap_addr;
    bi->mmap_length = 0;
    bi->mem_lower = mem_lower;
    bi->mem_upper = 0;

    struct boot_mmap_entry* mmap = (struct boot_mmap_entry*)(UINTN)mmap_addr;
    EFI_MEMORY_DESCRIPTOR* desc = map;

    for (UINTN i = 0; i < num_desc; i++) {
        if (bi->mmap_length >= mmap_max_entries) {
            break;
        }

        struct boot_mmap_entry* e = &mmap[bi->mmap_length];
        e->addr = desc->PhysicalStart;
        e->len = desc->NumberOfPages * 4096;

        switch (desc->Type) {
            case EfiConventionalMemory:
            case EfiBootServicesCode:
            case EfiBootServicesData:
                e->type = BOOT_MEM_AVAILABLE;
                if (e->addr >= mem_upper_min) {
                    bi->mem_upper += (uint32_t)(e->len / 1024);
                }
                break;
            case EfiACPIReclaimMemory: e->type = BOOT_MEM_ACPI; break;
            case EfiACPIMemoryNVS: e->type = BOOT_MEM_NVS; break;
            case EfiUnusableMemory: e->type = BOOT_MEM_BAD; break;
            default: e->type = BOOT_MEM_RESERVED; break;
        }

        bi->mmap_length++;
        desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)desc + desc_size);
    }

    /* Merge adjacent regions of the same type */
    uint32_t merged = 0;
    for (uint32_t i = 0; i < bi->mmap_length; i++) {
        if (merged > 0 && mmap[merged - 1].type == mmap[i].type &&
            mmap[merged - 1].addr + mmap[merged - 1].len == mmap[i].addr) {
            mmap[merged - 1].len += mmap[i].len;
            continue;
        }
        if (merged != i) {
            mmap[merged] = mmap[i];
        }
        merged++;
    }
    bi->mmap_length = merged;

    bs->FreePool(map);
    return EFI_SUCCESS;
}

static EFI_STATUS uefi_load_kernel_file(EFI_BOOT_SERVICES* bs, EFI_HANDLE image_handle, VOID** buf, UINTN* size) {
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image{};
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs{};
    EFI_FILE_PROTOCOL* root{};
    EFI_FILE_PROTOCOL* file{};

    EFI_GUID lip_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

    EFI_STATUS status = bs->HandleProtocol(image_handle, &lip_guid, (VOID**)&loaded_image);
    if (EFI_ERROR(status)) {
        return status;
    }

    status = bs->HandleProtocol(loaded_image->DeviceHandle, &fs_guid, (VOID**)&fs);
    if (EFI_ERROR(status)) {
        return status;
    }

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) {
        return status;
    }

    const CHAR16* paths[] = {UEFI_STR(L"\\EFI\\ZONIX\\KERNEL.ELF"), UEFI_STR(L"\\KERNEL.ELF"),
                             UEFI_STR(L"\\KERNEL.SYS"), 0};
    for (int i = 0; paths[i]; i++) {
        status = root->Open(root, &file, (CHAR16*)paths[i], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            break;
        }
    }
    if (EFI_ERROR(status)) {
        root->Close(root);
        return status;
    }

    EFI_GUID fi_guid = EFI_FILE_INFO_ID;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256;
    EFI_FILE_INFO* file_info{};

    status = bs->AllocatePool(EfiLoaderData, info_size, (VOID**)&file_info);
    if (EFI_ERROR(status)) {
        file->Close(file);
        root->Close(root);
        return status;
    }

    status = file->GetInfo(file, &fi_guid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        bs->FreePool(file_info);
        file->Close(file);
        root->Close(root);
        return status;
    }

    *size = (UINTN)file_info->FileSize;
    bs->FreePool(file_info);

    status = bs->AllocatePool(EfiLoaderData, *size, buf);
    if (EFI_ERROR(status)) {
        file->Close(file);
        root->Close(root);
        return status;
    }

    status = file->Read(file, size, *buf);
    file->Close(file);
    root->Close(root);
    return status;
}

static void uefi_get_graphics_info(EFI_BOOT_SERVICES* bs, struct boot_info* bi) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop{};
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    if (EFI_ERROR(bs->LocateProtocol(&gop_guid, nullptr, (VOID**)&gop))) {
        bi->framebuffer_addr = 0;
        return;
    }

    bi->framebuffer_addr = gop->Mode->FrameBufferBase;
    bi->framebuffer_width = gop->Mode->Info->HorizontalResolution;
    bi->framebuffer_height = gop->Mode->Info->VerticalResolution;
    bi->framebuffer_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    bi->framebuffer_bpp = 32;
    bi->framebuffer_type = 1; /* RGB */
}

static EFI_STATUS uefi_exit_boot_services(EFI_BOOT_SERVICES* bs, EFI_HANDLE image_handle) {
    UINTN map_key = 0, m_size = 0, d_size = 0;
    UINT32 d_ver = 0;
    EFI_MEMORY_DESCRIPTOR* m_map = NULL;

    bs->GetMemoryMap(&m_size, NULL, &map_key, &d_size, &d_ver);
    m_size += 2 * d_size;
    bs->AllocatePool(EfiLoaderData, m_size, (VOID**)&m_map);

    EFI_STATUS status = bs->GetMemoryMap(&m_size, m_map, &map_key, &d_size, &d_ver);
    if (EFI_ERROR(status)) {
        return status;
    }

    status = bs->ExitBootServices(image_handle, map_key);
    if (EFI_ERROR(status)) {
        /* Retry once -- map_key may have changed */
        m_size = 0;
        bs->GetMemoryMap(&m_size, NULL, &map_key, &d_size, &d_ver);
        m_size += 2 * d_size;
        bs->AllocatePool(EfiLoaderData, m_size, (VOID**)&m_map);
        bs->GetMemoryMap(&m_size, m_map, &map_key, &d_size, &d_ver);
        status = bs->ExitBootServices(image_handle, map_key);
    }
    return status;
}
