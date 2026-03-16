// UEFI Bootloader for Zonix (AArch64)
// EFI application that loads the kernel ELF and passes control with boot_info.

#include <uefi/uefi.h>
#include <kernel/bootinfo.h>
#include <base/elf.h>

// ---------- Minimal utility functions (freestanding, no libc) ----------

static void* memcpy(void* dst, const void* src, UINTN n) {
    UINT8* d = (UINT8*)dst;
    const UINT8* s = (const UINT8*)src;
    while (n--)
        *d++ = *s++;
    return dst;
}

static void* memset(void* dst, int c, UINTN n) {
    UINT8* d = (UINT8*)dst;
    while (n--)
        *d++ = (UINT8)c;
    return dst;
}

// ---------- Globals ----------

static EFI_SYSTEM_TABLE* ST;
static EFI_BOOT_SERVICES* BS;

static void print(const CHAR16* str) {
    if (ST && ST->ConOut)
        ST->ConOut->OutputString(ST->ConOut, (CHAR16*)str);
}

static void print_hex(UINT64 val) {
    CHAR16 buf[21];
    const CHAR16* hex = L"0123456789ABCDEF";
    buf[0] = L'0';
    buf[1] = L'x';
    for (int i = 0; i < 16; i++)
        buf[17 - i] = hex[(val >> (i * 4)) & 0xF];
    buf[18] = L'\r';
    buf[19] = L'\n';
    buf[20] = L'\0';
    print(buf);
}

// ---------- AArch64 kernel constants ----------

// Kernel is linked at KERNEL_BASE + physical.  Strip to get physical address.
#define KERNEL_VIRT_BASE 0xFFFF000000000000ULL

// Safe fixed physical addresses for boot data.
// QEMU virt RAM starts at 0x4000_0000; kernel loads at 0x4008_0000.
// Place boot_info and mmap well below the kernel.
#define SAFE_BOOT_INFO_ADDR   0x40005000ULL
#define SAFE_MMAP_ADDR        0x40005100ULL
#define SAFE_MMAP_MAX_ENTRIES ((0x40007000ULL - SAFE_MMAP_ADDR) / sizeof(struct boot_mmap_entry))

// ---------- ELF loader ----------

static int load_elf_kernel(void* elf_buffer, struct boot_info* boot_info) {
    elfhdr* elf = (elfhdr*)elf_buffer;
    if (elf->e_magic != ELF_MAGIC)
        return -1;

    UINT64 kernel_start = ~0ULL, kernel_end = 0;
    proghdr* ph = (proghdr*)((UINT8*)elf + elf->e_phoff);
    proghdr* eph = ph + elf->e_phnum;

    for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PT_LOAD)
            continue;

        UINT64 phys_addr = ph->p_pa;
        if (phys_addr >= KERNEL_VIRT_BASE)
            phys_addr = ph->p_va - KERNEL_VIRT_BASE;

        UINT8* dst = (UINT8*)(UINTN)phys_addr;
        UINT8* src = (UINT8*)elf + ph->p_offset;

        if (phys_addr < kernel_start)
            kernel_start = phys_addr;
        if (phys_addr + ph->p_memsz > kernel_end)
            kernel_end = phys_addr + ph->p_memsz;

        memcpy(dst, src, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz)
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
    }

    boot_info->kernel_start = (uint32_t)kernel_start;
    boot_info->kernel_end = (uint32_t)kernel_end;
    boot_info->kernel_entry = (uint32_t)(elf->e_entry - KERNEL_VIRT_BASE);
    return 0;
}

// ---------- UEFI memory map ----------

static EFI_STATUS get_memory_map(struct boot_info* boot_info) {
    UINTN map_key = 0, memory_map_size = 0, descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_MEMORY_DESCRIPTOR* memory_map = NULL;

    EFI_STATUS status = BS->GetMemoryMap(&memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL)
        return status;

    memory_map_size += 2 * descriptor_size;
    status = BS->AllocatePool(EfiLoaderData, memory_map_size, (VOID**)&memory_map);
    if (EFI_ERROR(status))
        return status;

    status = BS->GetMemoryMap(&memory_map_size, memory_map, &map_key, &descriptor_size, &descriptor_version);
    if (EFI_ERROR(status)) {
        BS->FreePool(memory_map);
        return status;
    }

    UINTN num_desc = memory_map_size / descriptor_size;
    boot_info->mmap_addr = SAFE_MMAP_ADDR;
    boot_info->mmap_length = 0;
    boot_info->mem_lower = 0;
    boot_info->mem_upper = 0;

    struct boot_mmap_entry* mmap = (struct boot_mmap_entry*)(UINTN)SAFE_MMAP_ADDR;
    EFI_MEMORY_DESCRIPTOR* desc = memory_map;

    for (UINTN i = 0; i < num_desc; i++) {
        if (boot_info->mmap_length >= SAFE_MMAP_MAX_ENTRIES)
            break;

        struct boot_mmap_entry* e = &mmap[boot_info->mmap_length];
        e->addr = desc->PhysicalStart;
        e->len = desc->NumberOfPages * 4096;

        switch (desc->Type) {
            case EfiConventionalMemory:
            case EfiBootServicesCode:
            case EfiBootServicesData:
                e->type = BOOT_MEM_AVAILABLE;
                boot_info->mem_upper += (uint32_t)(e->len / 1024);
                break;
            case EfiACPIReclaimMemory: e->type = BOOT_MEM_ACPI; break;
            case EfiACPIMemoryNVS: e->type = BOOT_MEM_NVS; break;
            case EfiUnusableMemory: e->type = BOOT_MEM_BAD; break;
            default: e->type = BOOT_MEM_RESERVED; break;
        }
        boot_info->mmap_length++;
        desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)desc + descriptor_size);
    }

    // Merge adjacent regions of the same type
    uint32_t merged = 0;
    for (uint32_t i = 0; i < boot_info->mmap_length; i++) {
        if (merged > 0) {
            struct boot_mmap_entry* prev = &mmap[merged - 1];
            struct boot_mmap_entry* cur = &mmap[i];
            if (prev->type == cur->type && prev->addr + prev->len == cur->addr) {
                prev->len += cur->len;
                continue;
            }
        }
        if (merged != i)
            mmap[merged] = mmap[i];
        merged++;
    }
    boot_info->mmap_length = merged;

    BS->FreePool(memory_map);
    return EFI_SUCCESS;
}

// ---------- Kernel file loading ----------

static EFI_STATUS load_kernel(EFI_HANDLE image_handle, VOID** kernel_buffer, UINTN* kernel_size) {
    EFI_LOADED_IMAGE_PROTOCOL* loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = NULL;
    EFI_FILE_PROTOCOL* root = NULL;
    EFI_FILE_PROTOCOL* kernel_file = NULL;

    EFI_GUID lip_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

    EFI_STATUS status = EFI_HandleProtocol(BS, image_handle, &lip_guid, (VOID**)&loaded_image);
    if (EFI_ERROR(status))
        return status;

    status = EFI_HandleProtocol(BS, loaded_image->DeviceHandle, &fs_guid, (VOID**)&fs);
    if (EFI_ERROR(status))
        return status;

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status))
        return status;

    const CHAR16* paths[] = {L"\\EFI\\ZONIX\\KERNEL.ELF", L"\\KERNEL.ELF", L"\\KERNEL.SYS", NULL};
    for (int i = 0; paths[i]; i++) {
        status = root->Open(root, &kernel_file, (CHAR16*)paths[i], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status))
            break;
    }
    if (EFI_ERROR(status)) {
        root->Close(root);
        return status;
    }

    EFI_GUID fi_guid = EFI_FILE_INFO_ID;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256;
    EFI_FILE_INFO* file_info = NULL;

    status = BS->AllocatePool(EfiLoaderData, info_size, (VOID**)&file_info);
    if (EFI_ERROR(status)) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    status = kernel_file->GetInfo(kernel_file, &fi_guid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        BS->FreePool(file_info);
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    *kernel_size = (UINTN)file_info->FileSize;
    BS->FreePool(file_info);

    status = BS->AllocatePool(EfiLoaderData, *kernel_size, kernel_buffer);
    if (EFI_ERROR(status)) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }

    status = kernel_file->Read(kernel_file, kernel_size, *kernel_buffer);
    kernel_file->Close(kernel_file);
    root->Close(root);
    return status;
}

// ---------- Graphics Output Protocol ----------

static void get_graphics_info(struct boot_info* boot_info) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = NULL;
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    if (EFI_ERROR(BS->LocateProtocol(&gop_guid, NULL, (VOID**)&gop))) {
        boot_info->framebuffer_addr = 0;
        return;
    }

    boot_info->framebuffer_addr = gop->Mode->FrameBufferBase;
    boot_info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
    boot_info->framebuffer_height = gop->Mode->Info->VerticalResolution;
    boot_info->framebuffer_pitch = gop->Mode->Info->PixelsPerScanLine * 4;
    boot_info->framebuffer_bpp = 32;
    boot_info->framebuffer_type = 1;  // RGB
}

// ---------- Exit Boot Services ----------

static EFI_STATUS exit_boot_services(EFI_HANDLE image_handle) {
    UINTN map_key = 0, m_size = 0, d_size = 0;
    UINT32 d_ver = 0;
    EFI_MEMORY_DESCRIPTOR* m_map = NULL;

    BS->GetMemoryMap(&m_size, NULL, &map_key, &d_size, &d_ver);
    m_size += 2 * d_size;
    BS->AllocatePool(EfiLoaderData, m_size, (VOID**)&m_map);

    EFI_STATUS status = BS->GetMemoryMap(&m_size, m_map, &map_key, &d_size, &d_ver);
    if (EFI_ERROR(status))
        return status;

    status = BS->ExitBootServices(image_handle, map_key);
    if (EFI_ERROR(status)) {
        // Retry once — map_key may have changed
        m_size = 0;
        BS->GetMemoryMap(&m_size, NULL, &map_key, &d_size, &d_ver);
        m_size += 2 * d_size;
        BS->AllocatePool(EfiLoaderData, m_size, (VOID**)&m_map);
        BS->GetMemoryMap(&m_size, m_map, &map_key, &d_size, &d_ver);
        status = BS->ExitBootServices(image_handle, map_key);
    }
    return status;
}

// ---------- UEFI entry point ----------

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE* system_table) {
    ST = system_table;
    BS = system_table->BootServices;

    ST->ConOut->ClearScreen(ST->ConOut);
    print(L"\r\nZonix UEFI Bootloader (AArch64) v1.0\r\n\r\n");
    BS->SetWatchdogTimer(0, 0, 0, NULL);

    // Prepare boot_info at fixed physical address (below kernel at 0x40080000)
    struct boot_info* boot_info = (struct boot_info*)(UINTN)SAFE_BOOT_INFO_ADDR;
    memset(boot_info, 0, sizeof(struct boot_info));
    boot_info->magic = BOOT_INFO_MAGIC;
    boot_info->mmap_addr = SAFE_MMAP_ADDR;

    // Memory map
    print(L"Getting memory map...\r\n");
    EFI_STATUS status = get_memory_map(boot_info);
    if (EFI_ERROR(status)) {
        print(L"Memory map failed\r\n");
        return status;
    }

    // Load kernel ELF from filesystem
    print(L"Loading kernel...\r\n");
    VOID* kernel_buffer = NULL;
    UINTN kernel_size = 0;
    status = load_kernel(image_handle, &kernel_buffer, &kernel_size);
    if (EFI_ERROR(status)) {
        print(L"Kernel load failed\r\n");
        return status;
    }

    // Parse ELF and copy segments to physical addresses (0x40080000+)
    print(L"Parsing ELF kernel...\r\n");

    // Allocate executable pages at the kernel's physical load address.
    // EDK2 on aarch64 marks EfiConventionalMemory as non-executable,
    // so we must claim the pages as EfiLoaderCode before writing code there.
    {
        EFI_PHYSICAL_ADDRESS kernel_base = 0x40080000ULL;
        UINTN kernel_pages = 256;  // 1MB — generous for the kernel image
        status = BS->AllocatePages(AllocateAddress, EfiLoaderCode, kernel_pages, &kernel_base);
        if (EFI_ERROR(status)) {
            print(L"AllocatePages for kernel failed\r\n");
            return status;
        }
    }

    if (load_elf_kernel(kernel_buffer, boot_info) != 0) {
        print(L"ELF parse failed\r\n");
        return EFI_LOAD_ERROR;
    }

    // Graphics (must be called before ExitBootServices)
    get_graphics_info(boot_info);

    // Loader name
    const char* name = "Zonix UEFI";
    for (int i = 0; name[i] && i < 31; i++)
        boot_info->loader_name[i] = name[i];

    print(L"Kernel entry (phys): ");
    print_hex(boot_info->kernel_entry);

    // Leave UEFI
    print(L"Exiting Boot Services...\r\n");
    status = exit_boot_services(image_handle);
    if (EFI_ERROR(status))
        return status;

    // Jump to kernel entry.
    // AArch64 UEFI and kernel both use AAPCS64 (x0 = first argument).
    // No ABI conversion needed (unlike x86 MS→SysV).
    typedef void (*kernel_entry_fn)(struct boot_info*);
    kernel_entry_fn entry = (kernel_entry_fn)(UINTN)boot_info->kernel_entry;
    entry(boot_info);

    // Should never return
    for (;;)
        __asm__ volatile("wfe");
    return EFI_SUCCESS;
}
