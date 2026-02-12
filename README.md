# Zonix OS

An x86_64 operating system kernel built from scratch for learning purposes, featuring dual-boot (BIOS + UEFI), process management, virtual memory with swap, interrupt-driven disk I/O, and FAT32 file system support.

**Current Version**: 0.8.0 *"Genesis"*

## Features

### Boot & Architecture
- **Dual Boot**: BIOS (MBR → VBR → bootloader) and UEFI (BOOTX64.EFI) boot paths
- **x86_64 Long Mode**: Full 64-bit kernel with higher-half mapping at `0xFFFFFFFF80000000`
- **Kconfig-style Configuration**: Modular `CONFIG_*` toggles in `include/kernel/config.h`
- **C++17 Freestanding**: Kernel written in C++17 with global `new`/`delete` operator support

### Process Management
- **Round-Robin Scheduler**: Cooperative scheduling with `fork()`, `exit()`, `wait()`
- **Kernel Threads**: `kernel_thread()` API with full context switch (callee-saved + CR3)
- **Process Hierarchy**: Parent-child links, zombie reaping, orphan reparenting to init
- **Process Table**: Hash table (1024 buckets) for O(1) PID lookup

### Memory Management
- **Physical Memory**: First-Fit page allocator with reference counting
- **Virtual Memory**: 4-level page table management, MMIO mapping, TLB invalidation
- **Page Fault Handler**: Demand paging with `vmm_pg_fault()` integration
- **Swap System**: FIFO page replacement with disk-backed swap I/O
- **kmalloc/kfree**: Page-granularity kernel heap with C++ `new`/`delete`

### File System
- **FAT32 Support**: Read-only FAT12/FAT16/FAT32 unified driver
- **Dual Mount Points**: System disk at `/`, secondary disk at `/mnt`
- **MBR Partition Detection**: Auto-detect MBR partition table and mount from partition

### Drivers
- **IDE/ATA**: 4-device PIO mode with interrupt-driven sleep/wakeup I/O
- **AHCI (SATA)**: MMIO-based SATA controller via PCI BAR discovery
- **PCI**: Configuration space read/write, device enumeration by class/subclass
- **8259 PIC / 8254 PIT**: Full initialization, EOI handling, timer ticks
- **PS/2 Keyboard**: Scancode mapping with interrupt-driven input
- **Display**: CGA text mode + GOP/VESA framebuffer console (PSF font rendering)
- **Serial**: COM1 debug output at 115200 baud

### Interrupt & Trap
- **IDT**: 256 entries with full x86_64 TrapFrame (all GPRs + hardware-pushed context)
- **Exceptions**: Named handlers for 20 CPU exceptions including page fault with CR2 output
- **IRQ Dispatch**: Timer, keyboard, IDE (primary + secondary) with automatic EOI

### Interactive Shell
- 20+ built-in commands including `ps`, `ls`, `cat`, `mount`, `lsblk`, `hdparm`
- Argument parsing (argc/argv), runs as dedicated kernel process (PID 2)
- Scheduler and swap system test commands (`schedtest`, `swaptest`)

## Environment

```bash
# BIOS mode
sudo apt install make gcc g++ nasm bochs-x dosfstools mtools

# UEFI mode (additional)
sudo apt install qemu-system-x86 ovmf
```

## Build

```bash
make            # Build kernel (BIOS)
make uefi       # Build UEFI bootloader
```

## Run

```bash
# Create disk images (first time only)
./tools/create_fat32_image.sh

# BIOS mode
bochs -f bochsrc.bxrc

# UEFI mode
make qemu-uefi

# Debug with GDB
make qemu-debug
```

## Shell Commands

### System Commands
| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `uname [-a]` | System information |
| `ps` | List all processes (PID, state, parent, stack, MM) |
| `clear` | Clear the screen |

### Disk & Storage
| Command | Description |
|---------|-------------|
| `lsblk` | List block devices with capacity |
| `hdparm` | Display disk geometry and I/O ports |
| `disktest` | Test disk read/write operations |
| `intrtest` | Test IDE interrupt functionality |

### File System
| Command | Description |
|---------|-------------|
| `mount <dev>` | Mount FAT filesystem (e.g., `mount hd1`) |
| `umount` | Unmount `/mnt` |
| `ls [/mnt]` | List files with attributes and sizes |
| `cat <file> [/mnt]` | Display file contents |
| `info` | Show filesystem information |

### Debug & Test
| Command | Description |
|---------|-------------|
| `pgdir` | Print page directory entries |
| `swaptest` | Run swap system tests |
| `schedtest` | Run scheduler tests |