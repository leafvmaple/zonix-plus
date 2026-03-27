#pragma once

using UINT8 = unsigned char;
using UINT16 = unsigned short;
using UINT32 = unsigned int;
using UINT64 = unsigned long long;
using INTN = signed long long;
using UINTN = unsigned long long;
using VOID = void;
using CHAR16 = wchar_t;
using EFI_STATUS = UINTN;
using EFI_HANDLE = VOID*;
using EFI_PHYSICAL_ADDRESS = UINT64;

[[nodiscard]] static constexpr const CHAR16* UEFI_STR(const wchar_t* s) {
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
    return static_cast<INTN>(status) < 0;
}

// =============================================================================
// GUID
// =============================================================================

struct EFI_GUID {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
};

[[nodiscard]] static constexpr EFI_GUID make_guid(UINT32 data1, UINT16 data2, UINT16 data3, UINT8 d0, UINT8 d1,
                                                  UINT8 d2, UINT8 d3, UINT8 d4, UINT8 d5, UINT8 d6, UINT8 d7) {
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

enum EFI_MEMORY_TYPE : UINT32 {
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
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
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

using EFI_TEXT_STRING = EFI_STATUS(EFIAPI*)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, CHAR16*);
using EFI_TEXT_CLEAR_SCREEN = EFI_STATUS(EFIAPI*)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
using EFI_TEXT_SET_ATTRIBUTE = EFI_STATUS(EFIAPI*)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, UINTN);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID* Reset;
    EFI_TEXT_STRING OutputString;
    VOID* TestString;
    VOID* QueryMode;
    VOID* SetMode;
    EFI_TEXT_SET_ATTRIBUTE SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    VOID* SetCursorPosition;
    VOID* EnableCursor;
    VOID* Mode;
};

// =============================================================================
// Boot Services
// =============================================================================

using EFI_ALLOCATE_POOL = EFI_STATUS(EFIAPI*)(EFI_MEMORY_TYPE, UINTN, VOID**);
using EFI_FREE_POOL = EFI_STATUS(EFIAPI*)(VOID*);
using EFI_GET_MEMORY_MAP = EFI_STATUS(EFIAPI*)(UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, UINT32*);
using EFI_HANDLE_PROTOCOL = EFI_STATUS(EFIAPI*)(EFI_HANDLE, EFI_GUID*, VOID**);
using EFI_LOCATE_PROTOCOL = EFI_STATUS(EFIAPI*)(EFI_GUID*, VOID*, VOID**);
using EFI_EXIT_BOOT_SERVICES = EFI_STATUS(EFIAPI*)(EFI_HANDLE, UINTN);
using EFI_SET_WATCHDOG_TIMER = EFI_STATUS(EFIAPI*)(UINTN, UINT64, UINTN, CHAR16*);

// AllocatePages Type values
inline constexpr UINTN AllocateAnyPages = 0;
inline constexpr UINTN AllocateMaxAddress = 1;
inline constexpr UINTN AllocateAddress = 2;

using EFI_ALLOCATE_PAGES = EFI_STATUS(EFIAPI*)(UINTN, EFI_MEMORY_TYPE, UINTN, EFI_PHYSICAL_ADDRESS*);

struct EFI_BOOT_SERVICES {
    UINT8 pad1[24];  // EFI_TABLE_HEADER (Signature+Revision+HeaderSize+CRC32+Reserved)
    VOID* pad2[2];   // RaiseTPL, RestoreTPL
    EFI_ALLOCATE_PAGES AllocatePages;
    VOID* FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    VOID* pad4[2];  // CreateEvent, SetTimer
    VOID* pad5[3];  // WaitForEvent, SignalEvent, CloseEvent
    VOID* pad6;     // CheckEvent
    VOID* pad7[3];  // InstallProtocolInterface, ReinstallProtocolInterface, UninstallProtocolInterface
    EFI_HANDLE_PROTOCOL HandleProtocol;
    VOID* Reserved;
    VOID* RegisterProtocolNotify;
    VOID* LocateHandle;
    VOID* LocateDevicePath;
    VOID* InstallConfigurationTable;
    VOID* pad8[4];  // LoadImage, StartImage, Exit, UnloadImage
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    VOID* pad9[2];  // GetNextMonotonicCount, Stall
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
    VOID* pad10[2];  // ConnectController, DisconnectController
    VOID* pad11[2];  // OpenProtocol, CloseProtocol
    VOID* OpenProtocolInformation;
    VOID* ProtocolsPerHandle;
    VOID* LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    VOID* pad12[2];  // InstallMultipleProtocolInterfaces, UninstallMultipleProtocolInterfaces
    VOID* CalculateCrc32;
    VOID* CopyMem;
    VOID* SetMem;
    VOID* CreateEventEx;
};

[[nodiscard]] static inline EFI_STATUS EFI_HandleProtocol(EFI_BOOT_SERVICES* bs, EFI_HANDLE handle, EFI_GUID* guid,
                                                          VOID** interface) {
    return bs->HandleProtocol(handle, guid, interface);
}

struct EFI_SYSTEM_TABLE {
    UINT8 pad1[48];  // EFI_TABLE_HEADER(24) + FirmwareVendor(8) + FirmwareRevision(4+4pad) + ConsoleInHandle(8)
    VOID* ConIn;
    VOID* ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    VOID* StandardErrorHandle;
    VOID* StdErr;
    VOID* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    UINTN NumberOfTableEntries;
    VOID* ConfigurationTable;
};

// =============================================================================
// File Protocol
// =============================================================================

inline constexpr UINT64 EFI_FILE_MODE_READ = 0x0000000000000001ULL;

struct EFI_FILE_INFO {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT8 pad[32];  // Time fields
    UINT64 Attribute;
    CHAR16 FileName[1];
};

struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS(EFIAPI* Open)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**, CHAR16*, UINT64, UINT64);
    EFI_STATUS(EFIAPI* Close)(EFI_FILE_PROTOCOL*);
    VOID* Delete;
    EFI_STATUS(EFIAPI* Read)(EFI_FILE_PROTOCOL*, UINTN*, VOID*);
    VOID* Write;
    VOID* GetPosition;
    VOID* SetPosition;
    EFI_STATUS(EFIAPI* GetInfo)(EFI_FILE_PROTOCOL*, EFI_GUID*, UINTN*, VOID*);
    VOID* SetInfo;
    VOID* Flush;
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS(EFIAPI* OpenVolume)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*, EFI_FILE_PROTOCOL**);
};

// =============================================================================
// Loaded Image Protocol
// =============================================================================

struct EFI_LOADED_IMAGE_PROTOCOL {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE* SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID* FilePath;
    VOID* Reserved;
    UINT32 LoadOptionsSize;
    VOID* LoadOptions;
    VOID* ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    VOID* Unload;
};

// =============================================================================
// Graphics Output Protocol
// =============================================================================

enum EFI_GRAPHICS_PIXEL_FORMAT : UINT32 {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax,
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    UINT8 pad[16];  // PixelInformation
    UINT32 PixelsPerScanLine;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    VOID* QueryMode;
    VOID* SetMode;
    VOID* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};
