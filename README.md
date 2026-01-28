# Zonix OS

A minimal x86 operating system with process scheduling, memory management, interrupt-driven I/O, and FAT32 file system support.

**Current Version**: 0.7.1

## Features

- **Process Scheduling**: Round-robin scheduler with sleep/wakeup mechanism
- **Memory Management**: Paging with swap support (FIFO algorithm)
- **Interrupt-Driven I/O**: IDE disk operations with hardware interrupts
- **Multi-Device Support**: Up to 4 IDE devices (Primary/Secondary Master/Slave)
- **FAT32 File System**: Read-only FAT32/FAT16/FAT12 support for disk files
- **Interactive Shell**: Built-in command-line interface

## Environment

```bash
sudo apt install make gcc nasm bochs-x
```

## Build

```bash
make
```

## Run

```bash
# Create FAT32 test image (first time only)
./tools/create_fat32_image.sh

# Run Zonix OS
bochs -f bochsrc.bxrc
```

## Shell Commands

### System Commands
- `help` - Show available commands
- `uname` - System information (-a for all)
- `ps` - List all processes
- `clear` - Clear the screen

### Disk Commands
- `lsblk` - List block devices
- `hdparm` - Display disk information
- `disktest` - Test disk read/write operations
- `intrtest` - Test IDE interrupt functionality

### File System Commands
- `mount <device>` - Mount FAT file system (e.g., mount hd1)
- `umount` - Unmount file system
- `ls [/mnt]` - List files in root directory
- `cat <filename> [/mnt]` - Display file contents
- `info` - Show file system information

### Memory Commands
- `pgdir` - Print page directory
- `swaptest` - Run swap system tests