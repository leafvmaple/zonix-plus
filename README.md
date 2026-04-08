# Zonix OS

A teaching operating system kernel targeting **x86_64**, **aarch64**, and **riscv64**, featuring BIOS/UEFI boot paths, process management, virtual memory with swap, synchronization primitives, preemptive scheduling, VFS-backed file syscalls, and FAT filesystem support.

**Current Version**: 0.11.1

## Features

### Boot & Architecture
- **Three Architectures**: x86_64, aarch64, riscv64 — shared kernel core with architecture-specific hooks under `arch/`
- **Dual Boot on x86**: BIOS (MBR → VBR → bootloader) and UEFI (BOOTX64.EFI)
- **UEFI on aarch64**: BOOTAA64.EFI + QEMU virt machine support
- **UEFI on riscv64**: BOOTRISCV64.EFI + QEMU virt / VisionFive2 board support
- **Kconfig-style Configuration**: Modular `CONFIG_*` toggles in `include/kernel/config.h`
- **C++17 Freestanding**: Kernel written in C++17 with global `new`/`delete` operator support
- **Clang/LLVM Toolchain**: Built with Clang, LLD, and LLVM utilities (including x86/aarch64/riscv64 UEFI paths)

### Process Management
- **Preemptive Round-Robin Scheduler**: Priority-aware scheduling with per-tick timeslice decrement
- **Kernel Threads**: `kernel_thread()` API with full context switch (callee-saved + CR3)
- **Process Hierarchy**: Parent-child links, zombie reaping, orphan reparenting to init
- **Process Table**: Hash table (1024 buckets) for O(1) PID lookup

### Synchronization
- **Spinlock**: Interrupt-safe atomic spinlock with architecture-abstracted spin hint
- **WaitQueue**: Structured sleep/wakeup mechanism (Linux kernel style `wait_queue_head_t`)
- **Semaphore**: Counting semaphore built on Spinlock + WaitQueue
- **Mutex**: Mutual exclusion lock with ownership tracking and assertion
- **LockGuard\<T\>**: Generic RAII lock guard template for any lockable type

### Memory Management
- **Physical Memory**: First-Fit page allocator with reference counting
- **Virtual Memory**: 4-level page table management, MMIO mapping, TLB invalidation
- **Page Fault Handler**: Demand paging with `vmm_pg_fault()` integration
- **Swap System**: FIFO page replacement with disk-backed swap I/O
- **kmalloc/kfree**: Page-granularity kernel heap with C++ `new`/`delete`

### File System
- **FAT32 Support**: Read-only FAT12/FAT16/FAT32 unified driver with split core/dir/VFS adapter modules
- **Dual Mount Points**: System disk at `/`, secondary disk at `/mnt`
- **MBR Partition Detection**: Auto-detect MBR partition table and mount from partition
- **VFS + FD Integration**: Task file context uses `fd::Table` under `kernel/fs` (decoupled from scheduler internals)

### Drivers
- **IDE/ATA**: 4-device PIO mode with interrupt-driven sleep/wakeup I/O
- **AHCI (SATA)**: MMIO-based SATA controller via PCI BAR discovery
- **SDHCI (aarch64/riscv64)**: SD card backend for QEMU virt UEFI flow
- **PL011 UART (aarch64)**: Serial console and early boot diagnostics
- **16550 UART (riscv64)**: Serial console for QEMU virt and VisionFive2
- **PCI**: Configuration space read/write, device enumeration by class/subclass
- **PLIC (riscv64)**: Platform-Level Interrupt Controller for external interrupts
- **8259 PIC / 8254 PIT**: Full initialization, EOI handling, timer ticks (x86)
- **PS/2 Keyboard**: Scancode mapping with interrupt-driven input (x86)
- **VirtIO Keyboard (riscv64)**: MMIO-based VirtIO input device
- **Display**: CGA text mode + GOP/VESA framebuffer console (PSF font rendering)
- **Serial**: COM1 debug output at 115200 baud

### Interrupt & Trap
- **IDT (x86)**: 256 entries with full x86_64 TrapFrame (all GPRs + hardware-pushed context)
- **Trap (riscv64)**: RISC-V supervisor trap handling with scause/stval dispatch
- **Exceptions**: Named handlers for CPU exceptions including page fault
- **IRQ Dispatch**: Timer, keyboard, IDE (primary + secondary) with automatic EOI
- **Architecture Abstraction**: `arch_*()` wrappers for interrupts, I/O, spin hints (portable across ISAs)

### Interactive Shell
- 20+ built-in commands including `ps`, `ls`, `cat`, `mount`, `lsblk`, `hdparm`
- Argument parsing (argc/argv), runs as dedicated kernel process (PID 2)
- Scheduler and swap system test commands (`schedtest`, `swaptest`)

## Environment

```bash
# Core build tools
sudo apt install make clang lld llvm nasm dosfstools mtools

# UEFI linker (Ubuntu 24.04 package name)
sudo apt install lld-18

# Emulators
sudo apt install qemu-system-x86 qemu-system-arm qemu-system-misc \
                 ovmf qemu-efi-aarch64 qemu-efi-riscv64 bochs-x
```

## Build

```bash
make ARCH=x86
make ARCH=aarch64
make ARCH=riscv64

# Include in-kernel unit tests
make ARCH=x86 TEST=1
```

## Run

```bash
# x86 UEFI + AHCI (default)
make qemu ARCH=x86

# x86 BIOS fallback
make qemu-bios ARCH=x86

# aarch64 UEFI (QEMU virt)
make qemu ARCH=aarch64

# riscv64 UEFI (QEMU virt)
make qemu ARCH=riscv64

# Debug with GDB
make debug ARCH=x86
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