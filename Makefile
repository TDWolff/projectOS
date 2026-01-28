# Makefile for Modular 64-bit Kernel (M1 Mac)

ASM = nasm
CC = clang
LD = x86_64-elf-ld

# 1. Flags
ASMFLAGS = -f elf64
CFLAGS = -target x86_64-pc-none-elf -ffreestanding -mno-red-zone -m64 \
         -mno-sse -mno-sse2 -mno-avx -mno-80387 -msoft-float \
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
iso: kernel.bin limine/limine terminal_app
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

run: app terminal_app iso
	qemu-system-x86_64 -cdrom os.iso -m 512M -vga std -display cocoa

app:
	make -f Makefile.apps

terminal_app:
	make -f Makefile.apps APP_NAME=terminal

clean:
	find . -type f -name "*.o" -delete
	rm -f kernel.bin os.iso
	rm -rf iso_root
	rm -f apps/*.o
	rm -f ../osstorage/stress_test.pexe
	rm -f osstorage/terminal.pexe