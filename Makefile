.SECONDEXPANSION:

CC		:= gcc
CXX		:= g++
CFLAGS	:= -g -fno-builtin -Wall -ggdb -O0 -m64 -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2 -mno-mmx -msoft-float -nostdinc -fno-stack-protector -fno-PIC -gdwarf-2
CXXFLAGS	:= -g -Wall -ggdb -O0 -m64 -mno-red-zone -mcmodel=kernel -mno-sse -mno-sse2 -mno-mmx -msoft-float -nostdinc -nostdinc++ -fno-builtin -fno-stack-protector \
			 -fno-PIC -fno-exceptions -fno-rtti -fno-use-cxa-atexit -fno-threadsafe-statics \
			 -fno-asynchronous-unwind-tables -fno-unwind-tables -ffreestanding -std=gnu++17 -gdwarf-2

# Boot code flags (32-bit, since BIOS bootloader runs in protected mode)
BOOT_CFLAGS := -g -fno-builtin -Wall -ggdb -O0 -m32 -nostdinc -fno-stack-protector -fno-PIC -gdwarf-2
BOOT_LDFLAGS := -m elf_i386 -nostdlib

HOSTCC		:= gcc
HOSTCFLAGS	:= -g -Wall -O2

LD      := ld
LDFLAGS := -m elf_x86_64 -nostdlib

DASM = ndisasm

QEMU := qemu-system-x86_64

OBJDUMP := objdump
OBJCOPY := objcopy
MKDIR   := mkdir -p

TERMINAL :=wt.exe wsl

ALLOBJS	:=  # 用来最终mkdir

SLASH	:= /
OBJDIR  := obj
BINDIR	:= bin

OBJPREFIX	:= __objs_
CTYPE	:= c S
CXXTYPE	:= cpp cc cxx

# dirs, #types
# $filter(%.type1 %.type2, dir1/* dir2/*)
listf = $(filter $(if $(2),$(addprefix %.,$(2)),%), $(wildcard $(addsuffix $(SLASH)*,$(1))))

# dirs
# $filter(%.c %.S, dir1/* dir2/*)
listf_cc = $(call listf,$(1),$(CTYPE))
listf_cxx = $(call listf,$(1),$(CXXTYPE))

# name1 name2 -> __objs_$(name1)
# __objs_
packetname = $(if $(1),$(addprefix $(OBJPREFIX),$(1)),$(OBJPREFIX))

# name1.* name2.*... -> obj/name1.o obj/name2.o
# name1.* name2.*..., dir -> obj/$(dir)/$(name1).o obj/$(dir)/$(name2).o)
toobj = $(addprefix $(OBJDIR)$(SLASH)$(if $(2),$(2)$(SLASH)),$(addsuffix .o,$(basename $(1))))

# file1 file2 -> bin/file1 bin/file2
totarget = $(addprefix $(BINDIR)$(SLASH),$(1))

# file1 file2 -> bin/file1.bin bin/file2.bin
tobin = $(addprefix $(BINDIR)$(SLASH),$(addsuffix .bin,$(1)))

# #files, cc, cflags
# obj/src/file1.o | src/file1.c | obj/src/
#	cc -Isrc cflags -c src/file1.c -o obj/src/file1.o
# ALLOBJS += obj/src/file1.o
define compile
$$(call toobj,$(1)): $(1) | $$$$(dir $$$$@)
	$(2) -I$$(dir $(1)) $(3) -c $$< -o $$@
ALLOBJS += $$(call toobj,$(1))
endef

compiles = $$(foreach f,$(1),$$(eval $$(call compile,$$(f),$(2),$(3))))

# #files, cc, cflags, packet
# __objs_$(packet) := obj/src/file1.o obj/src/file2.o...
# obj/src/file1.o:
#	cc -Isrc -Iinclude cflags -c src/file1.c -o obj/src/file1.o
# obj/src/file2.o:
#	cc -Isrc -Iinclude cflags -c src/file2.c -o obj/src/file2.o
define add_packet
__packet__ := $(call packetname,$(4))
$$(__packet__) += $(call toobj,$(1))
$(call compiles,$(1),$(2),$(3))
endef

# #packets
# obj/src/file1.o obj/src/file2.o...
read_packet = $(foreach p,$(call packetname,$(1)),$($(p)))

# #files, cc, cflas, packet
add_packet_files = $(eval $(call add_packet,$(1),$(2),$(3),$(4)))
# #files, packet
add_packet_files_cc = $(call add_packet_files,$(1),$(CC),$(CFLAGS),$(2))
add_packet_files_cxx = $(call add_packet_files,$(1),$(CXX),$(CXXFLAGS),$(2))

# obj/
#	mkdir -p obj/
# bin/
#	mkdir -p bin/
# obj/src/
#	mkdir -p obj/src/
define do_make_dir
$$(sort $$(dir $$(ALLOBJS)) $(BINDIR)$(SLASH) $(OBJDIR)$(SLASH)):
	$(MKDIR) $$@
endef
make_dir = $(eval $(call do_make_dir))

#####################################################################################

INCLUDE	+=  include  \
            kern/include

KSRCDIR :=	kern          \
            kern/arch/x86 \
			kern/debug    \
            kern/cons     \
            kern/trap     \
            kern/drivers  \
            kern/block    \
            kern/sched    \
            kern/mm       \
            kern/fs


CFLAGS	+= $(addprefix -I,$(INCLUDE))
CXXFLAGS	+= $(addprefix -I,$(INCLUDE))

$(call add_packet_files_cc,$(call listf_cc,init),initial)
$(call add_packet_files_cxx,$(call listf_cxx,init),initial)
$(call add_packet_files_cc,$(call listf_cc,$(KSRCDIR)),kernel)
$(call add_packet_files_cxx,$(call listf_cxx,$(KSRCDIR)),kernel)

kernel = $(call totarget,kernel)

KOBJS := $(sort $(call read_packet,initial) $(call read_packet,kernel))

$(kernel): $(KOBJS) tools/kernel.ld | $$(dir $$@)
	$(LD) $(LDFLAGS) -T tools/kernel.ld $(KOBJS) -o $@
	$(OBJDUMP) -D $@ > obj/kernel.asm
#	$(OBJDUMP) -t $@ | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$$/d' > obj/kernel.sym
	$(OBJCOPY) -S -O binary $@ $(call tobin,kernel)
	$(DASM) -b 64 $(call tobin,kernel) > obj/kernel.disasm

# MBR build (Master Boot Record) - BIOS mode
mbr = $(call totarget,mbr)
MOBJ = $(call toobj,boot/bios/mbr.S)

$(MOBJ): boot/bios/mbr.S | $$(dir $$@)
	$(CC) $(BOOT_CFLAGS) -I include -Os -c $< -o $@

$(mbr): $(MOBJ) tools/mbr.ld | $$(dir $$@)
	$(LD) $(BOOT_LDFLAGS) -T tools/mbr.ld -o $@ $<
	$(OBJDUMP) -D $@ > obj/mbr.asm
	$(OBJCOPY) -S -O binary $@ $(call tobin,mbr)
	$(DASM) -b 16 $(call tobin,mbr) > obj/mbr.disasm
	@# Check MBR size must be 512 bytes
	@if [ `stat -c%s $(call tobin,mbr)` -ne 512 ]; then \
		echo "Error: MBR size is not 512 bytes"; \
		exit 1; \
	fi

# VBR and Bootblock build (exclude mbr.S and bootload.c) - BIOS mode
bootfiles = $(filter-out boot/bios/mbr.S boot/bios/bootload.c, $(call listf_cc,boot/bios))
$(eval $(call compiles,$(bootfiles),$(CC),$(BOOT_CFLAGS) -I include -Os))

# VBR build (Volume Boot Record, formerly bootblock)
vbr = $(call totarget,vbr)
VOBJS = $(call toobj,$(bootfiles))

$(vbr): $(VOBJS) tools/boot.ld | $$(dir $$@)
	$(LD) $(BOOT_LDFLAGS) -T tools/boot.ld -o $@ $(VOBJS)
	$(OBJDUMP) -S $@ > obj/vbr.asm
	$(OBJCOPY) -S -O binary $@ $(call tobin,vbr)
	$(DASM) -b 16 $(call tobin,vbr) > obj/vbr.disasm
	@# Check VBR size must be exactly 512 bytes
	@if [ `stat -c%s $(call tobin,vbr)` -ne 512 ]; then \
		echo "Error: VBR size is `stat -c%s $(call tobin,vbr)` bytes, must be 512"; \
		exit 1; \
	fi

# Bootloader build (ELF loader at 0x7E00) - BIOS mode
# Can use reserved sectors: sector 1-7 (512 * 7 bytes total)
bootloader = $(call totarget,bootloader)
$(bootloader): boot/bios/bootload.c tools/bootload.ld | $$(dir $$@)
	$(CC) $(BOOT_CFLAGS) -I include -O2 -g -nostdinc -fno-builtin -fno-stack-protector -c boot/bios/bootload.c -o $(call toobj,boot/bios/bootload.c)
	$(LD) $(BOOT_LDFLAGS) -T tools/bootload.ld -o $@ $(call toobj,boot/bios/bootload.c)
	$(OBJDUMP) -S $@ > obj/bootloader.asm
	$(OBJCOPY) -S -O binary $@ $(call tobin,bootloader)
	$(DASM) -b 32 $(call tobin,bootloader) > obj/bootloader.disasm
	@# Check bootloader size must be <= 3584 bytes (3 reserved sectors)
	@if [ `stat -c%s $(call tobin,bootloader)` -gt 3584 ]; then \
		echo "Error: Bootloader size is `stat -c%s $(call tobin,bootloader)` bytes, must be <= 3584"; \
		exit 1; \
	fi

# UEFI Bootloader build (PE32+ format for x86_64 UEFI)
UEFI_CC := x86_64-w64-mingw32-gcc
UEFI_LD := ld
UEFI_OBJDUMP := objdump
UEFI_OBJCOPY := objcopy
UEFI_CFLAGS := -I include -I /usr/include/efi \
               -I /usr/include/efi/x86_64 \
               -nostdlib -ffreestanding -fshort-wchar -mno-red-zone -O2

UEFI_LDFLAGS := -Wl,--entry=efi_main -Wl,--subsystem,10 -Wl,--image-base,0 \
		    -L/usr/lib -lefi -lgnuefi

bootx64 = $(call totarget,BOOTX64.EFI)
$(bootx64): boot/uefi/bootload.c | $$(dir $$@)
	@mkdir -p obj/boot/uefi
	$(UEFI_CC) $(UEFI_CFLAGS) boot/uefi/bootload.c -o $@ $(UEFI_LDFLAGS)
	@echo "UEFI bootloader created: $@"


# Keep bootblock as compatibility target (points to VBR)
# boot = $(call totarget,bootblock)
BOBJS = $(call toobj,$(bootfiles))

$(call make_dir)

# FAT32 test disk image (ata0-slave)
bin/fat32_test.img:
	@echo "Creating FAT32 test disk image..."
	@bash tools/create_fat32_image.sh

# FAT32 disk image with proper filesystem
# Uses mtools to avoid sudo mount (install with: sudo apt install mtools)
bin/zonix.img: bin/mbr bin/vbr bin/bootloader bin/kernel | $$(dir $$@)
	@echo "Creating FAT32 disk image..."
	@# Create 64MB disk image (FAT32 requires at least 33MB)
	dd if=/dev/zero of=$@ bs=1M count=64 2>/dev/null
	@# Write MBR
	dd if=bin/mbr.bin of=$@ bs=446 count=1 conv=notrunc 2>/dev/null
	@# Create partition table (partition starts at sector 1)
	@echo -e "label: dos\nstart=1, size=131071, type=0c, bootable" | sfdisk $@ 2>/dev/null
	@# Format partition as FAT32
	mkfs.fat -F 32 -n "ZONIX" -S 512 -s 8 -R 32 -f 2 --offset 1 $@ 2>/dev/null
	@# Copy kernel using mtools (no sudo required)
	@# Configure mtools to access the partition at offset 512 (sector 1)
	@echo "drive z: file=\"$@\" offset=512" > /tmp/mtoolsrc_zonix
	MTOOLSRC=/tmp/mtoolsrc_zonix mcopy -i $@@@512 bin/kernel ::KERNEL.SYS
	@rm -f /tmp/mtoolsrc_zonix
	@# Install custom VBR boot code (preserve BPB)
	@# For FAT32, BPB is 90 bytes (0x00-0x59)
	@dd if=$@ of=temp_bpb.bin bs=1 skip=512 count=90 2>/dev/null
	@dd if=bin/vbr.bin of=temp_bootcode.bin bs=1 skip=90 count=420 2>/dev/null
	@dd if=bin/vbr.bin of=temp_signature.bin bs=1 skip=510 count=2 2>/dev/null
	@cat temp_bpb.bin temp_bootcode.bin temp_signature.bin > temp_vbr.bin
	@dd if=temp_vbr.bin of=$@ bs=1 seek=512 count=512 conv=notrunc 2>/dev/null
	@rm -f temp_bpb.bin temp_bootcode.bin temp_signature.bin temp_vbr.bin
	@# Install bootloader (after reserved sectors)
	@# Place at sector 2 for safety (offset = 1024)
	@dd if=bin/bootloader.bin of=$@ bs=1 seek=1024 conv=notrunc 2>/dev/null
	@echo "FAT32 image created: $@"

# UEFI disk image with GPT partition
bin/zonix-uefi.img: bin/BOOTX64.EFI bin/kernel | $$(dir $$@)
	@echo "Creating UEFI disk image..."
	@bash tools/create_uefi_image.sh
	@echo "UEFI disk image created: $@"

TARGETS: bin/mbr bin/vbr bin/bootloader bin/kernel bin/zonix.img bin/BOOTX64.EFI

# Additional disk images for testing
DISK_IMAGES := bin/fat32_test.img

# Legacy FAT16 targets (optional, build with 'make fat16')
FAT32_TARGETS: bin/zonix.img bin/fat32_test.img

# UEFI targets (optional, build with 'make uefi')
UEFI_TARGETS: bin/BOOTX64.EFI bin/zonix-uefi.img

.DEFAULT_GOAL := TARGETS

mbr: bin/mbr

vbr: bin/vbr

fat32: bin/zonix.img

# Build UEFI bootloader and image
uefi: $(UEFI_TARGETS)

qemu: bin/zonix.img
	$(QEMU) -readconfig qemu.cfg -no-reboot

qemu-ahci: bin/zonix.img
	$(QEMU) -readconfig qemu-ahci.cfg -S -no-reboot

qemu-fat32: bin/zonix.img
	$(QEMU) -S -no-reboot -readconfig qemu.cfg

qemu-uefi: bin/zonix-uefi.img
	$(QEMU) -bios /usr/share/ovmf/OVMF.fd -readconfig qemu-uefi.cfg
#	$(QEMU) -bios /usr/share/ovmf/OVMF.fd \
#	-drive file=bin/zonix-uefi.img,format=raw \
#	-m 256M -serial null -monitor stdio

debug-qemu-uefi: bin/zonix-uefi.img
	$(QEMU) -bios /usr/share/ovmf/OVMF.fd -readconfig qemu-uefi.cfg -S -s &
	sleep 2
	$(TERMINAL) -e gdb -q -x tools/gdbinit

debug-qemu: bin/zonix.img
	$(QEMU) -readconfig qemu-debug.cfg -S -s &
	sleep 2
	$(TERMINAL) -e "gdb -q -x tools/gdbinit"

debug-qemu-ahci: bin/zonix.img
	$(QEMU) -S -s -parallel stdio \
		-device ahci,id=ahci0 \
		-drive if=none,id=sata0,file=$<,format=raw \
		-device ide-hd,bus=ahci0.0,drive=sata0 \
		-drive if=ide,index=1,file=bin/fat32_test.img,format=raw \
		-serial null &
	sleep 2
	$(TERMINAL) -e "gdb -q -x tools/gdbinit"

# New default uses FAT32
bochs: bin/zonix.img $(DISK_IMAGES)
	bochs -q -f bochsrc.bxrc

debug-bochs: bin/zonix.img $(DISK_IMAGES)
	bochs -q -f bochsrc_debug.bxrc -dbg

gdb: bin/zonix.img $(DISK_IMAGES)
	$(TERMINAL) -e bochs -q -f bochsrc.bxrc
	sleep 2
	gdb -q -x tools/gdbinit

clean:
	rm -f -r obj bin/*.o bin/mbr bin/vbr bin/kernel bin/zonix.img bin/fat32_test.img bin/fat32_test.img bin/disk3.img bin/disk4.img