#include <boot/uefi_boot.h>
#include <base/elf.h>

/*
 * The compiler may emit implicit calls to memcpy/memset/memmove for
 * struct copies and zeroing.  Provide C-linkage wrappers so the linker
 * can resolve them without a C library.
 */
extern "C" void* memcpy(void* dst, const void* src, uintptr_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

extern "C" void* memset(void* dst, int c, uintptr_t n) {
    auto* d = static_cast<uint8_t*>(dst);
    while (n--) {
        *d++ = static_cast<uint8_t>(c);
    }
    return dst;
}

static void uefi_print(EFI_SYSTEM_TABLE* system_table, const wchar_t* str) {
    if (system_table && system_table->ConOut) {
        system_table->ConOut->OutputString(system_table->ConOut, const_cast<wchar_t*>(str));
    }
}

static void uefi_print_hex(EFI_SYSTEM_TABLE* system_table, uint64_t val) {
    wchar_t buf[21];
    const wchar_t* hex = uefi_string(L"0123456789ABCDEF");
    buf[0] = L'0';
    buf[1] = L'x';
    for (int i = 0; i < 16; i++) {
        buf[17 - i] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[18] = L'\r';
    buf[19] = L'\n';
    buf[20] = L'\0';
    uefi_print(system_table, buf);
}

static int uefi_load_elf(void* elf_buffer, struct BootInfo* boot_info, uint64_t kernel_base_va) {
    auto* elf = static_cast<ElfHeader*>(elf_buffer);
    if (elf->e_magic != ELF_MAGIC) {
        return -1;
    }

    uint64_t kernel_start_pa = ~0ULL, kernel_end_pa = 0;
    auto* ph = reinterpret_cast<ProgramHeader*>(reinterpret_cast<uint8_t*>(elf) + elf->e_phoff);
    ProgramHeader* ph_end = ph + elf->e_phnum;

    for (; ph < ph_end; ph++) {
        if (ph->p_type != ELF_PT_LOAD) {
            continue;
        }

        uint64_t segment_pa = ph->p_pa;
        if (segment_pa >= kernel_base_va) {
            segment_pa = ph->p_va - kernel_base_va;
        }

        auto* dst = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(segment_pa));
        auto* src = reinterpret_cast<uint8_t*>(elf) + ph->p_offset;

        if (segment_pa < kernel_start_pa) {
            kernel_start_pa = segment_pa;
        }
        if (segment_pa + ph->p_memsz > kernel_end_pa) {
            kernel_end_pa = segment_pa + ph->p_memsz;
        }

        memcpy(dst, src, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }
    }

    boot_info->kernel_start = static_cast<uint32_t>(kernel_start_pa);
    boot_info->kernel_end = static_cast<uint32_t>(kernel_end_pa);
    boot_info->kernel_entry = static_cast<uint32_t>(elf->e_entry - kernel_base_va);
    return 0;
}

static EFI_STATUS uefi_get_memory_map(EFI_BOOT_SERVICES* boot_services, struct BootInfo* boot_info,
                                      uint64_t memory_map_pa, uintptr_t memory_map_capacity, uint32_t lower_memory_kib,
                                      uint64_t upper_memory_start_pa) {
    uintptr_t map_key = 0;
    uintptr_t map_size_bytes = 0;
    uintptr_t desc_size_bytes = 0;
    uint32_t desc_version = 0;
    EFI_MEMORY_DESCRIPTOR* memory_map = nullptr;

    EFI_STATUS status =
        boot_services->GetMemoryMap(&map_size_bytes, memory_map, &map_key, &desc_size_bytes, &desc_version);
    if (status != EFI_BUFFER_TOO_SMALL) {
        return status;
    }

    map_size_bytes += 2 * desc_size_bytes;
    status = boot_services->AllocatePool(EfiLoaderData, map_size_bytes, reinterpret_cast<void**>(&memory_map));
    if (EFI_ERROR(status)) {
        return status;
    }

    status = boot_services->GetMemoryMap(&map_size_bytes, memory_map, &map_key, &desc_size_bytes, &desc_version);
    if (EFI_ERROR(status)) {
        boot_services->FreePool(memory_map);
        return status;
    }

    uintptr_t desc_count = map_size_bytes / desc_size_bytes;
    boot_info->mmap_addr = memory_map_pa;
    boot_info->mmap_length = 0;
    boot_info->mem_lower = lower_memory_kib;
    boot_info->mem_upper = 0;

    auto* mmap = reinterpret_cast<BootMemEntry*>(static_cast<uintptr_t>(memory_map_pa));
    EFI_MEMORY_DESCRIPTOR* desc = memory_map;

    for (uintptr_t i = 0; i < desc_count; i++) {
        if (boot_info->mmap_length >= memory_map_capacity) {
            break;
        }

        struct BootMemEntry* e = &mmap[boot_info->mmap_length];
        e->addr = desc->PhysicalStart;
        e->len = desc->NumberOfPages * 4096;

        switch (desc->Type) {
            case EfiConventionalMemory:
            case EfiBootServicesCode:
            case EfiBootServicesData:
                e->type = BOOT_MEM_AVAILABLE;
                if (e->addr >= upper_memory_start_pa) {
                    boot_info->mem_upper += static_cast<uint32_t>(e->len / 1024);
                }
                break;
            case EfiACPIReclaimMemory: e->type = BOOT_MEM_ACPI; break;
            case EfiACPIMemoryNVS: e->type = BOOT_MEM_NVS; break;
            case EfiUnusableMemory: e->type = BOOT_MEM_BAD; break;
            default: e->type = BOOT_MEM_RESERVED; break;
        }

        boot_info->mmap_length++;
        desc = reinterpret_cast<EFI_MEMORY_DESCRIPTOR*>(reinterpret_cast<uint8_t*>(desc) + desc_size_bytes);
    }

    /* Merge adjacent regions of the same type */
    uint32_t merged = 0;
    for (uint32_t i = 0; i < boot_info->mmap_length; i++) {
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
    boot_info->mmap_length = merged;

    boot_services->FreePool(memory_map);
    return EFI_SUCCESS;
}

static EFI_STATUS uefi_load_kernel_file(EFI_BOOT_SERVICES* boot_services, EFI_HANDLE image_handle, void** buf,
                                        uintptr_t* size) {
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image{};
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs{};
    EFI_FILE_PROTOCOL* root{};
    EFI_FILE_PROTOCOL* file{};

    EFI_GUID lip_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

    EFI_STATUS status = boot_services->HandleProtocol(image_handle, &lip_guid, reinterpret_cast<void**>(&loaded_image));
    if (EFI_ERROR(status)) {
        return status;
    }

    status = boot_services->HandleProtocol(loaded_image->DeviceHandle, &fs_guid, reinterpret_cast<void**>(&fs));
    if (EFI_ERROR(status)) {
        return status;
    }

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) {
        return status;
    }

    const wchar_t* paths[] = {uefi_string(L"\\EFI\\ZONIX\\KERNEL.ELF"), uefi_string(L"\\KERNEL.ELF"),
                              uefi_string(L"\\KERNEL.SYS"), nullptr};
    for (int i = 0; paths[i]; i++) {
        status = root->Open(root, &file, const_cast<wchar_t*>(paths[i]), EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            break;
        }
    }
    if (EFI_ERROR(status)) {
        root->Close(root);
        return status;
    }

    EFI_GUID fi_guid = EFI_FILE_INFO_ID;
    uintptr_t info_size = sizeof(EFI_FILE_INFO) + 256;
    EFI_FILE_INFO* file_info{};

    status = boot_services->AllocatePool(EfiLoaderData, info_size, reinterpret_cast<void**>(&file_info));
    if (EFI_ERROR(status)) {
        file->Close(file);
        root->Close(root);
        return status;
    }

    status = file->GetInfo(file, &fi_guid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        boot_services->FreePool(file_info);
        file->Close(file);
        root->Close(root);
        return status;
    }

    *size = static_cast<uintptr_t>(file_info->FileSize);
    boot_services->FreePool(file_info);

    status = boot_services->AllocatePool(EfiLoaderData, *size, buf);
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

static void uefi_get_graphics_info(EFI_BOOT_SERVICES* boot_services, struct BootInfo* boot_info) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop{};
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    if (EFI_ERROR(boot_services->LocateProtocol(&gop_guid, nullptr, reinterpret_cast<void**>(&gop)))) {
        boot_info->framebuffer_addr = 0;
        return;
    }

    boot_info->framebuffer_addr = gop->Mode->FrameBufferBase;
    boot_info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
    boot_info->framebuffer_height = gop->Mode->Info->VerticalResolution;
    boot_info->framebuffer_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    boot_info->framebuffer_bpp = 32;
    boot_info->framebuffer_type = 1; /* RGB */
}

static EFI_STATUS uefi_exit_boot_services(EFI_BOOT_SERVICES* boot_services, EFI_HANDLE image_handle) {
    uintptr_t map_key = 0, map_size_bytes = 0, desc_size_bytes = 0;
    uint32_t desc_version = 0;
    EFI_MEMORY_DESCRIPTOR* memory_map = nullptr;

    boot_services->GetMemoryMap(&map_size_bytes, nullptr, &map_key, &desc_size_bytes, &desc_version);
    map_size_bytes += 2 * desc_size_bytes;
    boot_services->AllocatePool(EfiLoaderData, map_size_bytes, reinterpret_cast<void**>(&memory_map));

    EFI_STATUS status =
        boot_services->GetMemoryMap(&map_size_bytes, memory_map, &map_key, &desc_size_bytes, &desc_version);
    if (EFI_ERROR(status)) {
        return status;
    }

    status = boot_services->ExitBootServices(image_handle, map_key);
    if (EFI_ERROR(status)) {
        /* Retry once -- map_key may have changed */
        map_size_bytes = 0;
        boot_services->GetMemoryMap(&map_size_bytes, nullptr, &map_key, &desc_size_bytes, &desc_version);
        map_size_bytes += 2 * desc_size_bytes;
        boot_services->AllocatePool(EfiLoaderData, map_size_bytes, reinterpret_cast<void**>(&memory_map));
        boot_services->GetMemoryMap(&map_size_bytes, memory_map, &map_key, &desc_size_bytes, &desc_version);
        status = boot_services->ExitBootServices(image_handle, map_key);
    }
    return status;
}

EFI_STATUS uefi_boot_setup(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table, const UefiBootConfig& config,
                           BootInfo** out_boot_info) {
    EFI_BOOT_SERVICES* boot_services = system_table->BootServices;

    system_table->ConOut->ClearScreen(system_table->ConOut);
    uefi_print(system_table, config.banner);
    boot_services->SetWatchdogTimer(0, 0, 0, nullptr);

    auto* boot_info = reinterpret_cast<BootInfo*>(static_cast<uintptr_t>(config.boot_info_pa));
    memset(boot_info, 0, sizeof(BootInfo));
    boot_info->magic = BOOT_INFO_MAGIC;
    boot_info->mmap_addr = config.memory_map_pa;

    uefi_print(system_table, uefi_string(L"Getting memory map...\r\n"));
    EFI_STATUS status = uefi_get_memory_map(boot_services, boot_info, config.memory_map_pa, config.memory_map_capacity,
                                            config.lower_memory_kib, config.upper_memory_start_pa);
    if (EFI_ERROR(status)) {
        uefi_print(system_table, uefi_string(L"Memory map failed\r\n"));
        return status;
    }

    uefi_print(system_table, uefi_string(L"Loading kernel...\r\n"));
    void* kernel_buf = nullptr;
    uintptr_t kernel_size_bytes = 0;
    status = uefi_load_kernel_file(boot_services, image_handle, &kernel_buf, &kernel_size_bytes);
    if (EFI_ERROR(status)) {
        uefi_print(system_table, uefi_string(L"Kernel load failed\r\n"));
        return status;
    }

    if (config.kernel_alloc_pa != 0) {
        EFI_PHYSICAL_ADDRESS kernel_alloc_pa = config.kernel_alloc_pa;
        status =
            boot_services->AllocatePages(AllocateAddress, EfiLoaderCode, config.kernel_page_count, &kernel_alloc_pa);
        if (EFI_ERROR(status)) {
            uefi_print(system_table, uefi_string(L"AllocatePages for kernel failed\r\n"));
            return status;
        }
    }

    uefi_print(system_table, uefi_string(L"Parsing ELF kernel...\r\n"));
    if (uefi_load_elf(kernel_buf, boot_info, config.kernel_base_va) != 0) {
        uefi_print(system_table, uefi_string(L"ELF parse failed\r\n"));
        return EFI_LOAD_ERROR;
    }

    uefi_get_graphics_info(boot_services, boot_info);

    for (int i = 0; config.loader_name[i] && i < 31; i++) {
        boot_info->loader_name[i] = config.loader_name[i];
    }

    uefi_print(system_table, uefi_string(L"Kernel entry (phys): "));
    uefi_print_hex(system_table, boot_info->kernel_entry);

    uefi_print(system_table, uefi_string(L"Exiting Boot Services...\r\n"));
    status = uefi_exit_boot_services(boot_services, image_handle);
    if (EFI_ERROR(status)) {
        return status;
    }

    *out_boot_info = boot_info;
    return EFI_SUCCESS;
}
