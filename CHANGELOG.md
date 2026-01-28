# Changelog

All notable changes to the Zonix Operating System project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.7.1] - 2025-12-04

### Changed
- **FAT32 File System Support**: Migrated from FAT16 to FAT32 as default filesystem
  - Updated bootloader to support FAT32 BPB structure (90-byte header)
  - Expanded bootloader reserved space to 7 sectors (3584 bytes)
  - Modified VBR to handle FAT32 root directory cluster chain
  - Disk image size increased to 64MB (FAT32 minimum requirement)
  - Updated BPB constants: 32 reserved sectors, 8 sectors per cluster
- **File System Driver Refactoring**: Renamed `fat16.*` to generic `fat.*`
  - Unified FAT12/FAT16/FAT32 support in single driver
  - Added FAT32-specific structures and cluster chain handling
  - Shell commands now support both FAT16 and FAT32 volumes
- **Build System Updates**: Default target now builds FAT32 images
  - Created `create_fat32_image.sh` for test image generation
  - Updated Makefile to use `bin/zonix.img` with FAT32 format
  - Bochs configuration updated for 64MB FAT32 disk geometry

### Technical Details
- FAT32 BPB offset 0x5A (90 bytes) vs FAT16 offset 0x3E (62 bytes)
- Root directory now cluster-based (cluster 2) instead of fixed sectors
- FAT entry size: 4 bytes (32-bit) with 28-bit cluster mask (0x0FFFFFFF)
- End-of-chain marker: 0x0FFFFFF8 for FAT32

## [0.7.0] - 2025-11-25

### Added
- **UEFI Boot Support**: Complete dual-boot architecture supporting both BIOS and UEFI modes
  - `boot/bios/`: BIOS legacy boot path (MBR → VBR → bootloader → kernel)
  - `boot/uefi/`: UEFI boot path (BOOTX64.EFI → kernel)
  - Unified `boot_info` structure for passing boot parameters to kernel
  - `include/kernel/bootinfo.h`: Common boot protocol with memory map, kernel info, framebuffer
  - `include/uefi/uefi.h`: Complete UEFI type definitions and protocol interfaces
- **Bootloader Capacity Expansion**: Extended bootloader from 512 bytes to 1536 bytes (3 sectors)
  - Utilizes FAT16 reserved sectors (sector 1-3) for larger bootloader code
  - VBR updated to load 3 sectors instead of 1
  - Supports more complex ELF loading and boot info preparation
- **Kernel Entry Point Updates**: Modified kernel initialization to accept boot_info parameter
  - `head.S`: Copies boot_info from bootloader to kernel BSS before paging changes
  - `kern_init()`: Now takes `struct boot_info*` parameter for runtime boot information

### Changed
- **Boot Architecture Reorganization**: Separated BIOS and UEFI bootloaders into dedicated directories
  - `boot/bios/mbr.S`: Master Boot Record for BIOS boot
  - `boot/bios/vbr.S`: Volume Boot Record with FAT16 support
  - `boot/bios/bootload.c`: BIOS-specific ELF loader at 0x7E00
  - `boot/uefi/bootload.c`: UEFI EFI application (PE32+ format)
- **Build System Enhancements**:
  - Added UEFI bootloader build rules (64-bit PE32+ format)
  - Created `bin/zonix-uefi.img` target with GPT partition and ESP
  - Added `qemu-uefi` make target for UEFI testing with OVMF firmware
  - Fixed Makefile path handling for new boot directory structure
- **Disk Image Layout** (BIOS):
  - Sector 0 (offset 512): VBR with BPB preserved
  - Sectors 1-3 (offset 1024-2560): Bootloader (1536 bytes)
  - Sectors 4+: FAT16 filesystem data

### Fixed
- **VBR Overwrite Bug**: Corrected bootloader installation offset in Makefile
  - Previously wrote bootloader at offset 512, overwriting VBR
  - Now correctly writes at offset 1024 (after VBR)
  - VBR signature (0xAA55) now properly preserved

### Technical Details
- UEFI bootloader built with `-fPIC -m64` for x86_64 EFI applications
- Boot info structure copied to kernel space before low memory unmapping
- E820 memory map location: 0x7000 (was 0x8000)
- UEFI images require OVMF firmware: `qemu-system-x86_64 -bios OVMF.fd`

## [0.6.2] - 2025-11-20

### Added
- **MBR Partition Support in FAT16**: Auto-detect and handle MBR-partitioned disks
  - `fat16_mount()` now checks if disk has MBR partition table
  - Reads first partition's start LBA and mounts FAT16 filesystem from partition
  - All sector reads automatically offset by partition start LBA
  - Supports system disk (hda) with MBR + FAT16 partition

### Changed
- **Shell Commands**: Redesigned filesystem commands to match Linux conventions
  - `ls` (was `fatls`): List files on system disk `/`, or `ls /mnt` for mounted disk
  - `cat <file>` (was `fatcat`): Read file from system disk, or `cat <file> /mnt` from mounted disk
  - `mount <device>` (was `fatmount`): Mount additional disk to `/mnt` (e.g., `mount hdb`)
  - `umount` (was `fatumount`): Unmount `/mnt`
  - `info` (was `fatinfo`): Show filesystem info for both `/` and `/mnt` (if mounted)
  - System disk (hda) auto-mounts on first access, cannot be mounted to `/mnt`

## [0.6.1] - 2025-11-20

### Fixed
- **Critical: PIC Initialization**: Added missing ICW1-4 initialization sequence in `pic_init()`
  - Previously only called `pic_enable(IRQ_SLAVE)` without proper ICW initialization
  - Now performs complete 8259 PIC setup: ICW1 (init), ICW2 (vector offset), ICW3 (cascade), ICW4 (mode)
  - Fixes potential spurious interrupts and undefined PIC behavior during kernel boot
  - Root cause of previous Double Fault (#8) issues during memory initialization

### Changed
- **Boot Architecture Refactoring**: Split VBR and bootloader into separate components
  - `boot/vbr.S`: FAT16-aware VBR (512 bytes, sector 1) loads kernel and bootloader from filesystem
  - `boot/bootload.c`: Standalone ELF loader (512 bytes, sector 2) loaded at 0x7E00 in protected mode
  - `boot/bpb.h`: Added FAT16 BPB constants and memory layout definitions
  - Removed obsolete `boot/bootasm.S` (replaced by vbr.S)
  - VBR now loads entire kernel file to 0x10000, then jumps to bootloader
  - Bootloader parses ELF, loads segments, zeroes BSS, and jumps to kernel entry
- **Build System**: Updated Makefile for new boot architecture
  - Added `bin/zonix-fat16.img` target with proper FAT16 filesystem
  - Separate build rules for VBR (boot.ld) and bootloader (bootload.ld)
  - Automated FAT16 image creation with VBR + bootloader + kernel
  - Updated bochs/gdb targets to use FAT16 image by default
  - Fixed `bootfiles` filter to exclude both mbr.S and bootload.c

### Technical Details
- **Memory Layout**: 
  - 0x7C00: VBR loaded by BIOS
  - 0x7E00: Bootloader loaded by VBR
  - 0x10000: Kernel ELF file loaded by VBR
  - Bootloader unpacks ELF segments and jumps to kernel entry
- **PIC Configuration**:
  - Master PIC: IRQ 0-7 mapped to INT 0x20-0x27
  - Slave PIC: IRQ 8-15 mapped to INT 0x28-0x2F
  - Auto EOI mode enabled (ICW4_AUTO | ICW4_8086)
  - All interrupts masked initially, only IRQ2 (cascade) enabled

## [0.6.0] - 2025-11-14

### Added
- **MBR (Master Boot Record) Support**: Implemented standard MBR boot sector
  - `boot/mbr.S`: MBR boot code with partition table support
  - `include/base/mbr.h`: MBR constants and partition type definitions
  - `tools/mbr.ld`: MBR linker script for 512-byte boot sector
  - Partition table scanning for active partition (0x80 flag)
  - BIOS INT 13h Extended Read (LBA addressing) with static packet buffer
  - MBR relocation from 0x7C00 to 0x0600 for compatibility
  - VBR (Volume Boot Record) loading from active partition to 0x7C00

### Changed
- **Boot Chain Architecture**: MBR → VBR → Kernel (was: VBR → Kernel)
  - `boot/bootasm.S`: Now functions as VBR (sector 1) instead of MBR
  - `boot/bootload.c`: Updated sector offset calculation (+2 instead of +1)
  - `Makefile`: Added MBR build targets and multi-sector disk image assembly
  - Disk layout: Sector 0 (MBR) + Sector 1 (VBR) + Sector 2+ (Kernel)

## [0.5.0] - 2025-11-12

### Added
- **FAT16 File System Support**: Implemented read-only FAT16/FAT12 file system
  - `kern/fs/fat16.h`: FAT16 data structures and API definitions
  - `kern/fs/fat16.c`: Core FAT16 implementation (~450 lines)
  - Mount FAT16 formatted disks
  - Directory listing with file attributes
  - File reading with cluster chain traversal
  - Automatic FAT12/FAT16 type detection
- **File System Shell Commands**:
  - `fatmount`: Mount FAT16 file system from disk
  - `fatls`: List files in root directory with attributes and sizes
  - `fatcat <filename>`: Display file contents
- **Testing Infrastructure**:
  - `tools/create_fat16_image.sh`: Automated FAT16 test image creation
  - Creates 16MB FAT16 disk with sample files
  - Pre-populated test files (HELLO.TXT, README.TXT, etc.)
- **Documentation**:
  - `docs/FAT16_GUIDE.md`: Complete FAT16 implementation guide
  - `FAT16_QUICKSTART.md`: Quick start guide for testing
  - Architecture diagrams and API reference

### Changed
- **Build System**: Added `kern/fs` directory to compilation paths
- **Bochs Configuration**: Updated `bochsrc.bxrc` to use FAT16 test image as ata0-slave

### Technical Details
- Boot sector parsing with validation (0xAA55 signature)
- FAT table caching (one sector at a time)
- 8.3 filename format support
- Cluster-to-sector address translation
- Directory entry validation and filtering
- Support for disks up to 2GB (FAT16 limit)
- Read buffer size: 4KB per operation

## [0.4.1] - 2025-11-12

### Changed
- **Shell Argument Parsing**: Refactored command processing to support proper argument handling
  - Added `parse_args()`: Split command line into argc/argv
  - Updated command function signature: `void (*func)(int argc, char **argv)`
  - Implemented `strcmp()` for exact command matching
  - Removed command order dependency (no more `"uname -a"` vs `"uname"` issue)
- **`uname` Command**: Unified into single command with flag support
  - `uname` displays kernel name only
  - `uname -a` displays all system information
  - Merged `cmd_uname` and `cmd_uname_a` implementations

### Technical Details
- Command table simplified from 12 to 11 entries
- Arguments parsed with space-delimited tokenization
- Maximum 16 arguments supported (MAX_ARGS)
- Foundation for future commands with complex parameters (e.g., `dd if=... of=...`)

## [0.4.0] - 2025-11-11

### Added
- **Interrupt-Driven IDE Disk I/O**: Implemented full interrupt-based disk operations
  - `hd_intr()`: Hardware interrupt handler for IDE controllers (IRQ 14/15)
  - Interrupt-driven `hd_read_device()` and `hd_write_device()`
  - Process sleep/wakeup mechanism for I/O completion
  - Added control register (IDE_CTRL) support with interrupt enable/disable
  - Added IDE_DSC (Drive Seek Complete) status bit definition
- **IDE Device Management**: Enhanced multi-device support
  - Control register tracking per device (`ctrl` field in `ide_device_t`)
  - Interrupt state fields: `irq_done`, `err`, `buffer`, `op`, `waiting`
  - Proper interrupt enable/disable during device detection
- **Shell Commands**: Added disk testing utilities
  - `intrtest`: Test IDE interrupt functionality
  - `hdparm`: Display detailed disk information

### Fixed
- **IDE Interrupt Race Condition**: Fixed critical timing issue in write operations
  - Reset `irq_done` **before** writing data (not after)
  - Prevents completion interrupt arriving before flag is cleared
- **Interrupt State Management**: Cleaned up redundant interrupt save/restore pairs
  - Removed unnecessary `intr_save()`/`intr_restore()` sequences
  - Simplified wait loop interrupt handling

### Changed
- **IDE Configuration**: Updated device initialization
  - Added control register ports to `ide_configs` table
  - Explicit interrupt disable during IDENTIFY command
  - Re-enable interrupts after successful device detection

## [0.3.0] - 2025-10-21

### Added
- **Process Scheduling**: Implemented complete round-robin scheduler
  - `schedule()`: Find and switch to next runnable process
  - `proc_run()`: Context switch with CR3 and register switching
  - `switch_to()`: Assembly routine for low-level context switch
- **Process Context Switch**: Working idle ↔ init process switching
  - Fixed `current` pointer update timing (before `switch_to`)
  - Fixed `trapret` to match trapframe structure (removed invalid segment register pops)
  - Fixed `forkret` to directly jump without extra `call`

### Fixed
- **Critical Scheduling Bug**: Process switching now works correctly
  - Updated `current` pointer **before** `switch_to()` (not after)
  - Fixed trapframe restoration in `forkret`/`trapret`
  - Removed bogus segment register pops that corrupted stack
- **Shell Prompt Timing**: Prompt now displays after full system initialization
  - Moved prompt to init process (executes after all initialization complete)

### Technical Details
- Context structure: eip, esp, ebx, ecx, edx, esi, edi, ebp
- Trapframe structure: 8 general regs + trapno + errcode + eip + cs + eflags + esp + ss
- Init process (PID 1) yields CPU via continuous `schedule()` calls

## [0.2.2] - 2025-10-21

### Fixed
- **BSS Segment Initialization**: Fixed uninitialized global/static variables
  - Added BSS clearing code in `init/head.S` (follows Linux kernel pattern)
  - Defined `__bss_start` and `__bss_end` symbols in linker script
  - Root cause: Bootloader loads segments but doesn't zero BSS
- **Debug Support**: Added Bochs port e9 output to `cons_putc()` for logging

## [0.2.1] - 2025-10-21

### Added
- **Init Process (PID 1)**: Created via `do_fork()` with `init_main()` entry point
- **Hash Table Lookup**: `find_proc()` for O(1) PID lookup (25-50x faster than list traversal)
- **List Helper**: `list_prev()` for backwards traversal

### Changed
- Renamed `PID_HASH()` to `pid_hashfn()` for Linux naming consistency
- Removed `NR_TASKS` limit for dynamic process creation
- Fixed `print_all_procs()` ordering with `list_prev()`

### Improved
- All processes created through unified `do_fork()` path
- Simplified `do_fork()` error handling

## [0.2.0] - 2025-10-21

### Added
- **Process Management System**
  - Complete `task_struct` with 18 fields (states, relationships, context)
  - Idle process (PID 0) and process lifecycle functions
  - Context switching in `kern/sched/switch.S`
  - Hash table (1024 buckets) and process list
  - `proc_get_cr3()` for dynamic page directory lookup
- **Shell Commands**
  - `ps` command: Display process info (PID, STATE, PPID, KSTACK, MM, NAME)
- **Console I/O**
  - Signed integer support in `cprintf()` with `%d` format
  - Proper negative number handling with padding
- **Memory Management**
  - Physical memory manager with first-fit algorithm
  - Virtual memory with page table management
  - Swap system with FIFO replacement policy
  - Demand paging and page fault handling
- **Interrupt & Trap**
  - IDT initialization with 256 entries
  - Trap frame for interrupt context
  - Page fault handler (interrupt 14)
- **Drivers**
  - CGA text mode display (80x25)
  - Keyboard input via i8042 controller
  - PIT timer for scheduling
  - ATA hard disk driver (up to 4 disks)

### Changed
- Updated memory manager to use `init_mm` as kernel address space
- Refactored page table operations for clarity

## [0.1.0] - 2025-10-20

### Added
- **Basic Kernel Infrastructure**
  - Bootloader with ELF loading
  - Protected mode with GDT/IDT
  - Simple page table (4MB mapping)
  - Basic console I/O (`cprintf`, CGA driver)
  - Interrupt handling framework
- **Build System**
  - Makefile with separate compilation
  - Bochs configuration for testing
  - Linker scripts for bootloader and kernel

### Initial Release
First working kernel that boots and displays output.
