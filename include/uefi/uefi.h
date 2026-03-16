#pragma once

// Minimal UEFI definitions for bootloader
// Only includes essential types and protocols needed for boot

// =============================================================================
// Basic Types
// =============================================================================

typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;
typedef signed long long INTN;
typedef unsigned long long UINTN;
typedef void VOID;
typedef unsigned short CHAR16;
typedef UINTN EFI_STATUS;
typedef VOID* EFI_HANDLE;
typedef UINT64 EFI_PHYSICAL_ADDRESS;

#define TRUE  1
#define FALSE 0
#define IN
#define OUT

// x86_64: UEFI uses Microsoft ABI, kernel uses System V — must annotate.
// AArch64: UEFI and kernel both use AAPCS64 — no annotation needed.
#if defined(__x86_64__) || defined(_M_X64)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

// Status codes
#define EFI_SUCCESS           0
#define EFI_LOAD_ERROR        (1 | (1ULL << 63))
#define EFI_INVALID_PARAMETER (2 | (1ULL << 63))
#define EFI_BUFFER_TOO_SMALL  (5 | (1ULL << 63))
#define EFI_NOT_FOUND         (14 | (1ULL << 63))
#define EFI_ERROR(status)     ((INTN)(status) < 0)

// =============================================================================
// GUID
// =============================================================================

typedef struct {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

#define GUID_INIT(a, b, c, d0, d1, d2, d3, d4, d5, d6, d7) \
    {                                                      \
        a, b, c, {                                         \
            d0, d1, d2, d3, d4, d5, d6, d7                 \
        }                                                  \
    }

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    GUID_INIT(0x5b1b31a1, 0x9562, 0x11d2, 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    GUID_INIT(0x964e5b22, 0x6459, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_FILE_INFO_ID GUID_INIT(0x9576e92, 0x6d3f, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    GUID_INIT(0x9042a9de, 0x23dc, 0x4a38, 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a)

// =============================================================================
// Memory Types
// =============================================================================

typedef enum {
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
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    UINT64 VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

// =============================================================================
// Protocol Forward Declarations
// =============================================================================

typedef struct _EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;
typedef struct _EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct _EFI_LOADED_IMAGE_PROTOCOL EFI_LOADED_IMAGE_PROTOCOL;
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

// =============================================================================
// Text Output Protocol
// =============================================================================

typedef EFI_STATUS(EFIAPI* EFI_TEXT_STRING)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, IN CHAR16*);
typedef EFI_STATUS(EFIAPI* EFI_TEXT_CLEAR_SCREEN)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
typedef EFI_STATUS(EFIAPI* EFI_TEXT_SET_ATTRIBUTE)(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, IN UINTN);

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
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

typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_POOL)(IN EFI_MEMORY_TYPE, IN UINTN, OUT VOID**);
typedef EFI_STATUS(EFIAPI* EFI_FREE_POOL)(IN VOID*);
typedef EFI_STATUS(EFIAPI* EFI_GET_MEMORY_MAP)(IN OUT UINTN*, OUT EFI_MEMORY_DESCRIPTOR*, OUT UINTN*, OUT UINTN*,
                                               OUT UINT32*);
typedef EFI_STATUS(EFIAPI* EFI_HANDLE_PROTOCOL)(IN EFI_HANDLE, IN EFI_GUID*, OUT VOID**);
typedef EFI_STATUS(EFIAPI* EFI_LOCATE_PROTOCOL)(IN EFI_GUID*, IN VOID*, OUT VOID**);
typedef EFI_STATUS(EFIAPI* EFI_EXIT_BOOT_SERVICES)(IN EFI_HANDLE, IN UINTN);
typedef EFI_STATUS(EFIAPI* EFI_SET_WATCHDOG_TIMER)(IN UINTN, IN UINT64, IN UINTN, IN CHAR16*);

// AllocatePages Type values
#define AllocateAnyPages   0
#define AllocateMaxAddress 1
#define AllocateAddress    2

typedef EFI_STATUS(EFIAPI* EFI_ALLOCATE_PAGES)(IN UINTN, IN EFI_MEMORY_TYPE, IN UINTN, IN OUT EFI_PHYSICAL_ADDRESS*);

struct _EFI_BOOT_SERVICES {
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

// =============================================================================
// Helper macro to get HandleProtocol from BootServices
// =============================================================================

static inline EFI_STATUS EFI_HandleProtocol(EFI_BOOT_SERVICES* bs, EFI_HANDLE handle, EFI_GUID* guid,
                                            VOID** interface) {
    return bs->HandleProtocol(handle, guid, interface);
}

struct _EFI_SYSTEM_TABLE {
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

#define EFI_FILE_MODE_READ 0x0000000000000001ULL

typedef struct {
    UINT64 Size;
    UINT64 FileSize;
    UINT64 PhysicalSize;
    UINT8 pad[32];  // Time fields
    UINT64 Attribute;
    CHAR16 FileName[1];
} EFI_FILE_INFO;

struct _EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS(EFIAPI* Open)(struct _EFI_FILE_PROTOCOL*, struct _EFI_FILE_PROTOCOL**, CHAR16*, UINT64, UINT64);
    EFI_STATUS(EFIAPI* Close)(struct _EFI_FILE_PROTOCOL*);
    VOID* Delete;
    EFI_STATUS(EFIAPI* Read)(struct _EFI_FILE_PROTOCOL*, UINTN*, VOID*);
    VOID* Write;
    VOID* GetPosition;
    VOID* SetPosition;
    EFI_STATUS(EFIAPI* GetInfo)(struct _EFI_FILE_PROTOCOL*, EFI_GUID*, UINTN*, VOID*);
    VOID* SetInfo;
    VOID* Flush;
};

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS(EFIAPI* OpenVolume)(struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*, EFI_FILE_PROTOCOL**);
};

// =============================================================================
// Loaded Image Protocol
// =============================================================================

struct _EFI_LOADED_IMAGE_PROTOCOL {
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

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    UINT8 pad[16];  // PixelInformation
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    VOID* QueryMode;
    VOID* SetMode;
    VOID* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};
