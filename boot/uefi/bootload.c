// UEFI Bootloader for Zonix
// EFI application that loads zonix kernel and passes control

#include <efi.h>
#include <efilib.h>
#include <kernel/bootinfo.h>
#include <base/elf.h>

// Global UEFI state
EFI_SYSTEM_TABLE* ST = NULL;
EFI_BOOT_SERVICES* BS = NULL;

// Utility functions
static void* memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void* memset(void* dst, int c, size_t n) {
    char* d = (char*)dst;
    while (n--) *d++ = (char)c;
    return dst;
}

static void print(const CHAR16 *str) {
    if (ST && ST->ConOut) {
        ST->ConOut->OutputString(ST->ConOut, (CHAR16*)str);
    }
}

static void print_hex(uint64_t val) {
    CHAR16 buf[21];
    CHAR16 *hex_chars = L"0123456789ABCDEF";
    buf[0] = L'0';
    buf[1] = L'x';
    for (int i = 0; i < 16; i++) {
        buf[17 - i] = hex_chars[(val >> (i * 4)) & 0xF];
    }
    buf[18] = L'\r';
    buf[19] = L'\n';
    buf[20] = L'\0';
    print(buf);
}

// Load ELF kernel
static int load_elf_kernel(void* elf_buffer, struct boot_info* boot_info) {
    elfhdr* elf = (elfhdr*)elf_buffer;
    
    if (elf->e_magic != ELF_MAGIC) return -1;
    
    uint32_t kernel_start = 0xFFFFFFFF, kernel_end = 0;
    proghdr* ph = (proghdr*)((uint8_t*)elf + elf->e_phoff);
    proghdr* eph = ph + elf->e_phnum;
    
    for (; ph < eph; ph++) {
        if (ph->p_type != ELF_PT_LOAD) continue;
        
        uint8_t* dst = (uint8_t*)(ph->p_va & 0x00FFFFFF);
        uint8_t* src = (uint8_t*)elf + ph->p_offset;
        
        uint32_t phys_addr = (uint32_t)dst;
        if (phys_addr < kernel_start) kernel_start = phys_addr;
        if (phys_addr + ph->p_memsz > kernel_end) kernel_end = phys_addr + ph->p_memsz;
        
        memcpy(dst, src, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }
    }
    
    if (boot_info) {
        boot_info->kernel_start = kernel_start;
        boot_info->kernel_end = kernel_end;
        boot_info->kernel_entry = elf->e_entry;
    }
    
    return 0;
}

// Get UEFI memory map and convert to boot_info format
static EFI_STATUS get_memory_map(struct boot_info *boot_info, UINTN *map_key) {
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    UINTN memory_map_size = 0;
    UINTN descriptor_size = 0;
    UINT32 descriptor_version = 0;
    EFI_STATUS status;
    
    status = BS->GetMemoryMap(&memory_map_size, memory_map, map_key, &descriptor_size, &descriptor_version);
    if (status != EFI_BUFFER_TOO_SMALL) return status;
    
    memory_map_size += 2 * descriptor_size;
    status = BS->AllocatePool(EfiLoaderData, memory_map_size, (VOID**)&memory_map);
    if (EFI_ERROR(status)) return status;
    
    status = BS->GetMemoryMap(&memory_map_size, memory_map, map_key, &descriptor_size, &descriptor_version);
    if (EFI_ERROR(status)) {
        BS->FreePool(memory_map);
        return status;
    }
    
    UINTN num_desc = memory_map_size / descriptor_size;
    status = BS->AllocatePool(EfiLoaderData, num_desc * sizeof(struct boot_mmap_entry), (VOID**)&boot_info->mmap);
    if (EFI_ERROR(status)) {
        BS->FreePool(memory_map);
        return status;
    }
    
    boot_info->mmap_length = 0;
    boot_info->mem_lower = 640;
    boot_info->mem_upper = 0;
    
    EFI_MEMORY_DESCRIPTOR *desc = memory_map;
    for (UINTN i = 0; i < num_desc; i++) {
        struct boot_mmap_entry *entry = &boot_info->mmap[boot_info->mmap_length];
        entry->addr = desc->PhysicalStart;
        entry->len = desc->NumberOfPages * 4096;
        
        switch (desc->Type) {
            case EfiConventionalMemory:
            case EfiBootServicesCode:
            case EfiBootServicesData:
                entry->type = BOOT_MEM_AVAILABLE;
                if (entry->addr >= 0x100000) {
                    boot_info->mem_upper += (entry->len / 1024);
                }
                break;
            case EfiACPIReclaimMemory: entry->type = BOOT_MEM_ACPI; break;
            case EfiACPIMemoryNVS: entry->type = BOOT_MEM_NVS; break;
            case EfiUnusableMemory: entry->type = BOOT_MEM_BAD; break;
            default: entry->type = BOOT_MEM_RESERVED; break;
        }
        
        boot_info->mmap_length++;
        desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)desc + descriptor_size);
    }
    
    BS->FreePool(memory_map);
    return EFI_SUCCESS;
}

// Load kernel from UEFI filesystem
static EFI_STATUS load_kernel(EFI_HANDLE image_handle, VOID **kernel_buffer, UINTN *kernel_size) {
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = NULL;
    EFI_FILE_PROTOCOL *root = NULL, *kernel_file = NULL;
    EFI_STATUS status;
    
    EFI_GUID lip_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    
    status = BS->HandleProtocol(image_handle, &lip_guid, (VOID**)&loaded_image);
    if (EFI_ERROR(status)) return status;
    
    status = BS->HandleProtocol(loaded_image->DeviceHandle, &fs_guid, (VOID**)&fs);
    if (EFI_ERROR(status)) return status;
    
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status)) return status;
    
    const CHAR16 *paths[] = {L"\\EFI\\ZONIX\\KERNEL.ELF", L"\\KERNEL.ELF", L"\\KERNEL.SYS", NULL};
    for (int i = 0; paths[i]; i++) {
        status = root->Open(root, &kernel_file, (CHAR16*)paths[i], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) break;
    }
    if (EFI_ERROR(status)) {
        root->Close(root);
        return status;
    }
    
    EFI_GUID file_info_guid = EFI_FILE_INFO_ID;
    UINTN info_size = sizeof(EFI_FILE_INFO) + 256;
    EFI_FILE_INFO *file_info = NULL;
    
    status = BS->AllocatePool(EfiLoaderData, info_size, (VOID**)&file_info);
    if (EFI_ERROR(status)) {
        kernel_file->Close(kernel_file);
        root->Close(root);
        return status;
    }
    
    status = kernel_file->GetInfo(kernel_file, &file_info_guid, &info_size, file_info);
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

// Get graphics info from GOP
static void get_graphics_info(struct boot_info *boot_info) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = NULL;
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
    boot_info->framebuffer_type = 1;
}

static void exit_boot_services(EFI_HANDLE image_handle) {
    UINTN final_map_key = 0;
    UINTN m_size = 0, d_size = 0;
    UINT32 d_ver = 0;
    EFI_MEMORY_DESCRIPTOR *m_map = NULL;
    EFI_STATUS status;

    BS->GetMemoryMap(&m_size, NULL, &final_map_key, &d_size, &d_ver);

    m_size += 2 * d_size;
    BS->AllocatePool(EfiLoaderData, m_size, (VOID**)&m_map);

    status = BS->GetMemoryMap(&m_size, m_map, &final_map_key, &d_size, &d_ver);
}

// UEFI application entry point
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    ST = SystemTable;
    BS = SystemTable->BootServices;
    
    ST->ConOut->ClearScreen(ST->ConOut);
    print(L"\r\nZonix UEFI Bootloader v1.0\r\n\r\n");
    
    BS->SetWatchdogTimer(0, 0, 0, NULL);
    
    struct boot_info *boot_info = NULL;
    EFI_STATUS status = BS->AllocatePool(EfiLoaderData, sizeof(struct boot_info), (VOID**)&boot_info);
    if (EFI_ERROR(status)) return status;
    
    memset(boot_info, 0, sizeof(struct boot_info));
    boot_info->magic = BOOT_INFO_MAGIC;
    
    const char *name = "Zonix UEFI";
    for (int i = 0; name[i] && i < 31; i++) {
        boot_info->loader_name[i] = name[i];
    }
    
    UINTN map_key = 0;
    print(L"Getting memory map...\r\n");
    status = get_memory_map(boot_info, &map_key);
    if (EFI_ERROR(status)) return status;
    
    print(L"Loading kernel...\r\n");
    VOID *kernel_buffer = NULL;
    UINTN kernel_size = 0;
    status = load_kernel(ImageHandle, &kernel_buffer, &kernel_size);
    if (EFI_ERROR(status)) return status;
    
    print(L"Parsing ELF kernel...\r\n");
    if (load_elf_kernel(kernel_buffer, boot_info) != 0) {
        return EFI_LOAD_ERROR;
    }
    
    get_graphics_info(boot_info);

    print_hex(boot_info->kernel_entry & 0x00FFFFFF);

    print(L"Exiting Boot Services...\r\n");
    exit_boot_services(ImageHandle);
    
    kernel_entry_t kernel_entry = (kernel_entry_t)(boot_info->kernel_entry & 0x00FFFFFF);
    kernel_entry(boot_info);
    
    return EFI_SUCCESS;
}
