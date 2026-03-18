# Makefile for Modular 64-bit Kernel (M1 Mac)

ASM = nasm
CC = clang
LD = x86_64-elf-ld

# 1. Flags
ASMFLAGS = -f elf64
CFLAGS = -target x86_64-pc-none-elf -ffreestanding -mno-red-zone -m64 \
         -Isrc/include \
         -O2 -Wall -Wextra

LDFLAGS = -m elf_x86_64 -T linker.ld

# 2. Automatically find all sources
SRC_C = $(shell find src -name "*.c")
SRC_ASM_ALL = $(shell find . -name "*.asm")
OBJ = $(SRC_ASM_ALL:.asm=.o) $(SRC_C:.c=.o)

# 3. Targets
all: kernel.bin

# Compile Assembly
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link Kernel
kernel.bin: $(OBJ)
	$(LD) $(LDFLAGS) -o kernel.bin $(OBJ)

# Limine Tool
limine/limine:
	$(CC) -O2 limine/limine.c -o limine/limine

# Create ISO
iso: kernel.bin limine/limine
	# 1. Start with a clean slate
	rm -rf iso_root
	mkdir -p iso_root/boot
	mkdir -p iso_root/EFI/BOOT
	
	# 2. Generate limine.conf from the template
	cp limine.conf.template iso_root/limine.conf
	
	# 3. Copy kernel
	cp kernel.bin iso_root/
	
	# 4. Automatically process osstorage
	@mkdir -p osstorage
	@for file in $$(ls osstorage); do \
		echo "Adding to ISO: $$file"; \
		cp osstorage/$$file iso_root/$$file; \
		echo "    module_path: boot():/$$file" >> iso_root/limine.conf; \
		echo "    module_string: $$file" >> iso_root/limine.conf; \
	done

	#4.5. Copy Goha Classic font to the iso from iso_root
	cp gohaclassic-16.psf iso_root/

	# 5. Copy Limine binaries
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/

	# 6. Build and install bootloader
	xorriso -as mkisofs -b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o os.iso
	./limine/limine bios-install os.iso

run: app iso
	# Create a persistent IDE disk image if missing.
	@if [ ! -f disk.img ]; then \
		echo "Creating disk.img (64MB)..."; \
		dd if=/dev/zero of=disk.img bs=1m count=64 status=none; \
	fi
	qemu-system-x86_64 -cdrom os.iso -m 512M -vga std -display cocoa \
		-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-boot order=d,menu=on

# Same as run, but with serial logs and no automatic reboot.
run-debug: app iso
	@if [ ! -f disk.img ]; then \
		echo "Creating disk.img (64MB)..."; \
		dd if=/dev/zero of=disk.img bs=1m count=64 status=none; \
	fi
		qemu-system-x86_64 \
		-cdrom os.iso \
		-m 512M \
		-vga std \
		-display cocoa \
		-drive file=disk.img,format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-boot order=d,menu=on \
		-serial vc \
		-no-reboot

# Host helper: create/partition/format disk.img and copy a settings.pset into it.
# Requires: gdisk (or sgdisk), mtools (mcopy), and a FAT mkfs (mkfs.fat).
# On macOS you can usually get these via Homebrew.
disk-init:
	@echo "(Re)initializing disk.img as GPT + FAT32..."
	@rm -f disk.img
	@dd if=/dev/zero of=disk.img bs=1m count=64 status=none
	@# Create a single FAT32 partition starting at LBA 2048.
	@# Prefer sgdisk if present.
	@if command -v sgdisk >/dev/null 2>&1; then \
		sgdisk -o -n 1:2048:0 -t 1:0700 -c 1:user disk.img >/dev/null; \
	else \
		echo "sgdisk not found; please install gptfdisk (provides sgdisk)."; \
		exit 1; \
	fi
	@# Format the partition region as FAT32 using mtools (no loop-mount needed).
	@# Partition starts at 2048 * 512 = 1048576 bytes.
	@if command -v mformat >/dev/null 2>&1; then \
		MTOOLS_SKIP_CHECK=1 mformat -i disk.img@@1048576 -F :: >/dev/null; \
	else \
		echo "mtools not found; please install mtools (mformat/mcopy)."; \
		exit 1; \
	fi
	@if [ -f osstorage/settings.pset ]; then \
		MTOOLS_SKIP_CHECK=1 mcopy -i disk.img@@1048576 osstorage/settings.pset ::settings.pset >/dev/null; \
		echo "Seeded /user/settings.pset from osstorage/settings.pset"; \
	else \
		echo "Note: osstorage/settings.pset not found; skipping seed copy."; \
	fi

app:
	make -f Makefile.apps

clean:
	find . -type f -name "*.o" -delete
	rm -f kernel.bin os.iso
	rm -rf iso_root
	rm -f apps/*.o
	rm -f osstorage/stress_test.pexe