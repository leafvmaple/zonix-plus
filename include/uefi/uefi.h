#pragma once

#include <base/types.h>

using EFI_STATUS = uintptr_t;
using EFI_HANDLE = void*;
using EFI_PHYSICAL_ADDRESS = uint64_t;

[[nodiscard]] static constexpr const wchar_t* UEFI_STR(const wchar_t* s) {
    return s;
}

#if defined(__x86_64__) || defined(_M_X64)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

inline constexpr EFI_STATUS EFI_SUCCESS = 0;
inline constexpr EFI_STATUS EFI_LOAD_ERROR = (1ULL | (1ULL << 63));
inline constexpr EFI_STATUS EFI_INVALID_PARAMETER = (2ULL | (1ULL << 63));
inline constexpr EFI_STATUS EFI_BUFFER_TOO_SMALL = (5ULL | (1ULL << 63));
inline constexpr EFI_STATUS EFI_NOT_FOUND = (14ULL | (1ULL << 63));

[[nodiscard]] static constexpr bool EFI_ERROR(EFI_STATUS status) {
    return static_cast<intptr_t>(status) < 0;
}

// =============================================================================
// GUID
// =============================================================================

struct EFI_GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
};

[[nodiscard]] static constexpr EFI_GUID make_guid(uint32_t data1, uint16_t data2, uint16_t data3, uint8_t d0, uint8_t d1,
                                                  uint8_t d2, uint8_t d3, uint8_t d4, uint8_t d5, uint8_t d6, uint8_t d7) {
    return EFI_GUID{data1, data2, data3, {d0, d1, d2, d3, d4, d5, d6, d7}};
}

inline constexpr EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID =
    make_guid(0x5b1b31a1, 0x9562, 0x11d2, 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b);

inline constexpr EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID =
    make_guid(0x964e5b22, 0x6459, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b);

inline constexpr EFI_GUID EFI_FILE_INFO_ID =
    make_guid(0x09576e92, 0x6d3f, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b);

inline constexpr EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID =
    make_guid(0x9042a9de, 0x23dc, 0x4a38, 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a);

// =============================================================================
// Memory Types
// =============================================================================

enum EFI_MEMORY_TYPE : uint32_t {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType,
};

struct EFI_MEMORY_DESCRIPTOR {
    uint32_t Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
};

// =============================================================================
// Protocol Forward Declarations
// =============================================================================

struct EFI_SYSTEM_TABLE;
struct EFI_BOOT_SERVICES;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
struct EFI_FILE_PROTOCOL;
struct EFI_LOADED_IMAGE_PROTOCOL;
struct EFI_GRAPHICS_OUTPUT_PROTOCOL;

// =============================================================================
// Text Output Protocol
// =============================================================================

using EFI_TEXT_STRING = EFI_STATUS(EFIAPI*)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, wchar_t*);
using EFI_TEXT_CLEAR_SCREEN = EFI_STATUS(EFIAPI*)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
using EFI_TEXT_SET_ATTRIBUTE = EFI_STATUS(EFIAPI*)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, uintptr_t);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void* Reset;
    EFI_TEXT_STRING OutputString;
    void* TestString;
    void* QueryMode;
    void* SetMode;
    EFI_TEXT_SET_ATTRIBUTE SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    void* SetCursorPosition;
    void* EnableCursor;
    void* Mode;
};

// =============================================================================
// Boot Services
// =============================================================================

using EFI_ALLOCATE_POOL = EFI_STATUS(EFIAPI*)(EFI_MEMORY_TYPE, uintptr_t, void**);
using EFI_FREE_POOL = EFI_STATUS(EFIAPI*)(void*);
using EFI_GET_MEMORY_MAP = EFI_STATUS(EFIAPI*)(uintptr_t*, EFI_MEMORY_DESCRIPTOR*, uintptr_t*, uintptr_t*, uint32_t*);
using EFI_HANDLE_PROTOCOL = EFI_STATUS(EFIAPI*)(EFI_HANDLE, EFI_GUID*, void**);
using EFI_LOCATE_PROTOCOL = EFI_STATUS(EFIAPI*)(EFI_GUID*, void*, void**);
using EFI_EXIT_BOOT_SERVICES = EFI_STATUS(EFIAPI*)(EFI_HANDLE, uintptr_t);
using EFI_SET_WATCHDOG_TIMER = EFI_STATUS(EFIAPI*)(uintptr_t, uint64_t, uintptr_t, wchar_t*);

// AllocatePages Type values
inline constexpr uintptr_t AllocateAnyPages = 0;
inline constexpr uintptr_t AllocateMaxAddress = 1;
inline constexpr uintptr_t AllocateAddress = 2;

using EFI_ALLOCATE_PAGES = EFI_STATUS(EFIAPI*)(uintptr_t, EFI_MEMORY_TYPE, uintptr_t, EFI_PHYSICAL_ADDRESS*);

struct EFI_BOOT_SERVICES {
    uint8_t pad1[24];  // EFI_TABLE_HEADER (Signature+Revision+HeaderSize+CRC32+Reserved)
    void* pad2[2];     // RaiseTPL, RestoreTPL
    EFI_ALLOCATE_PAGES AllocatePages;
    void* FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void* pad4[2];  // CreateEvent, SetTimer
    void* pad5[3];  // WaitForEvent, SignalEvent, CloseEvent
    void* pad6;     // CheckEvent
    void* pad7[3];  // InstallProtocolInterface, ReinstallProtocolInterface, UninstallProtocolInterface
    EFI_HANDLE_PROTOCOL HandleProtocol;
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    void* pad8[4];  // LoadImage, StartImage, Exit, UnloadImage
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void* pad9[2];  // GetNextMonotonicCount, Stall
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
    void* pad10[2];  // ConnectController, DisconnectController
    void* pad11[2];  // OpenProtocol, CloseProtocol
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    void* LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void* pad12[2];  // InstallMultipleProtocolInterfaces, UninstallMultipleProtocolInterfaces
    void* CalculateCrc32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
};

[[nodiscard]] static inline EFI_STATUS EFI_HandleProtocol(EFI_BOOT_SERVICES* bs, EFI_HANDLE handle, EFI_GUID* guid,
                                                          void** interface) {
    return bs->HandleProtocol(handle, guid, interface);
}

struct EFI_SYSTEM_TABLE {
    uint8_t pad1[48];  // EFI_TABLE_HEADER(24) + FirmwareVendor(8) + FirmwareRevision(4+4pad) + ConsoleInHandle(8)
    void* ConIn;
    void* ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    void* StandardErrorHandle;
    void* StdErr;
    void* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    uintptr_t NumberOfTableEntries;
    void* ConfigurationTable;
};

// =============================================================================
// File Protocol
// =============================================================================

inline constexpr uint64_t EFI_FILE_MODE_READ = 0x0000000000000001ULL;

struct EFI_FILE_INFO {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    uint8_t pad[32];  // Time fields
    uint64_t Attribute;
    wchar_t FileName[1];
};

struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS(EFIAPI* Open)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**, wchar_t*, uint64_t, uint64_t);
    EFI_STATUS(EFIAPI* Close)(EFI_FILE_PROTOCOL*);
    void* Delete;
    EFI_STATUS(EFIAPI* Read)(EFI_FILE_PROTOCOL*, uintptr_t*, void*);
    void* Write;
    void* GetPosition;
    void* SetPosition;
    EFI_STATUS(EFIAPI* GetInfo)(EFI_FILE_PROTOCOL*, EFI_GUID*, uintptr_t*, void*);
    void* SetInfo;
    void* Flush;
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS(EFIAPI* OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*, EFI_FILE_PROTOCOL**);
};

// =============================================================================
// Loaded Image Protocol
// =============================================================================

struct EFI_LOADED_IMAGE_PROTOCOL {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    uint32_t LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    uint64_t ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void* Unload;
};

// =============================================================================
// Graphics Output Protocol
// =============================================================================

enum EFI_GRAPHICS_PIXEL_FORMAT : uint32_t {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax,
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    uint8_t pad[16];  // PixelInformation
    uint32_t PixelsPerScanLine;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    uintptr_t SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    uintptr_t FrameBufferSize;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void* QueryMode;
    void* SetMode;
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};
