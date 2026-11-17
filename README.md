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
- **C++20 Freestanding**: Kernel and C++ boot code use `gnu++20`, with global `new`/`delete` operator support
- **Clang/LLVM Toolchain**: Built with Clang, LLD, and LLVM utilities (including x86/aarch64/riscv64 UEFI paths)

### Process Management
- **Preemptive Round-Robin Scheduler**: Priority-aware scheduling with per-tick timeslice decrement
- **Kernel Threads**: `kernel_thread()` API with full context switch (callee-saved + CR3)
- **User Programs**: Compiler-independent static ELF ABI with shared runtime; Clang-built base components and selectable zcc/Clang application builds on all three architectures; argc/argv, spawn/wait/kill and inherited shared file descriptions ([ABI contract](docs/USER_ABI.md))
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
- Normal boot runs Clang-built userspace init (PID 1) and shell (PID 2) from an independent base rootfs
- Quoted arguments, per-process current directory, foreground execution and exit status
- Init restarts the shell and reaps orphaned children; the kernel debug shell remains in `TEST=1`
- Clang-built userspace utilities: `ls`, `cat`, `cp`, `mkdir`, `rm`, `sync`, `shutdown`, `ps`, `kill`, `vi`
- Build/install instructions and current limits: [Userspace](docs/USERSPACE.md)

## Environment

```bash
# Core build tools
sudo apt install make clang lld llvm nasm dosfstools mtools gdisk python3 flex bison

# UEFI linker (Ubuntu 24.04 package name)
sudo apt install lld-18

# Emulators
sudo apt install qemu-system-x86 qemu-system-arm qemu-system-misc \
                 ovmf qemu-efi-aarch64 qemu-efi-riscv64 bochs-x
```

### macOS (Intel / Apple Silicon)

Install the native emulator and cross-compilation tools with Homebrew:

```bash
brew install llvm lld qemu nasm dosfstools mtools gptfdisk flex bison coreutils

git submodule update --init --recursive
make install-hooks
make ARCH=x86 TEST=0 -j8
make rootfs ARCH=x86 TEST=0
make qemu ARCH=x86
```

The Makefile finds Homebrew's LLVM, LLD, Bison, Flex and filesystem tools without
changing your shell configuration. All kernels use ELF cross-compilation;
QEMU uses software emulation, including x86 on Apple Silicon. Firmware is found
in the Linux package locations or QEMU's bundled `share/qemu` directory. Override
`OVMF` (x86), `AAVMF` (aarch64/riscv64) and `AAVMF_VARS` (riscv64) if needed.
Disk images are created without root privileges or loop mounts.

On macOS the default Cocoa window enables `zoom-to-fit`, so its edges can be
dragged to resize it. Fullscreen and quitting are handled by QEMU:
release captured input with Control+Option+G, toggle fullscreen through the View
menu (Command+F), and select **Quit** in QEMU's exit confirmation dialog (the
default button is **Cancel**).

```bash
# Native QEMU window; serial console remains in this terminal
make qemu ARCH=aarch64
make qemu ARCH=riscv64

# Serial output only; use the QEMU window for keyboard input
make qemu ARCH=x86 QEMU_FLAGS="-display none"

# Existing in-kernel tests, with bounded headless QEMU execution
make ARCH=x86 TEST=1 -j8
bash scripts/ci_qemu_test.sh bios
bash scripts/ci_qemu_test.sh uefi
make ARCH=aarch64 TEST=1 bin/aarch64/sdcard.img -j8
bash scripts/ci_qemu_test_aarch64.sh
make ARCH=riscv64 TEST=1 bin/riscv64/sdcard.img -j8
bash scripts/ci_qemu_test_riscv64.sh
```

## Build

```bash
make ARCH=x86
make ARCH=aarch64
make ARCH=riscv64

# Include in-kernel unit tests
make ARCH=x86 TEST=1

# Show detailed loader and kernel startup logs
make ARCH=x86 BOOT_VERBOSE=1
```

Normal boot displays concise summaries and warnings/errors. All kernel log
levels are retained in a fixed 128-record buffer; use `dmesg` to read them and
`loglevel debug` to show future debug messages on the console. Set
`BOOT_VERBOSE=0` to restore normal boot output; changing this option rebuilds
the affected objects automatically. Shell/program output and `TEST=1` reports
are independent of the log threshold.

## Run

Prepare the base rootfs with `make rootfs ARCH=<arch> TEST=0` before normal boot. Extra applications are optional: `make user-image ARCH=<arch>` creates `apps.img`; attach it with `USER_DISK=bin/<arch>/apps.img`. Boot-image builds depend on neither; see [Userspace](docs/USERSPACE.md).

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

## Kernel Debug Shell Commands (`TEST=1`)

Normal boot uses the [userspace shell](docs/USERSPACE.md#当前-shell). The following commands belong to the retained kernel debug shell.

### System Commands
| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `dmesg` | Show retained kernel logs, including hidden debug messages |
| `loglevel [debug\|info\|warn\|error]` | Show or change the console log threshold |
| `uname [-a]` | System information |
| `ps` | List all processes (PID, state, parent, stack, MM) |
| `clear` | Clear the screen |
| `shutdown` | Power off the system (no arguments) |

`shutdown` stops new storage operations, drains in-flight file and raw block I/O,
synchronizes mounted filesystems and device caches, then requests platform
power-off. A synchronization failure returns an error and keeps the shell usable.
The current power-control implementations support the configured x86 `pc` machine (PIIX4 PM), aarch64
`virt` (PSCI over HVC), and riscv64 firmware providing the SBI System Reset
extension. Unsupported power control reports an error and returns to the shell.

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
| `sync` | Synchronize mounted filesystems and device caches; report errors |

P0-2 shutdown/reboot regressions run on temporary disk copies after building
`TEST=1` images: `python3 scripts/test_shutdown_qemu.py x86 --boot bios --unsupported`,
`python3 scripts/test_shutdown_qemu.py x86 --boot uefi`, or the same script with
`aarch64` / `riscv64`. Logs remain under `obj/<arch>/shutdown-<boot>/`.

### Debug & Test
| Command | Description |
|---------|-------------|
| `pgdir` | Print page directory entries |
| `swaptest` | Run swap system tests |
| `schedtest` | Run scheduler tests |
