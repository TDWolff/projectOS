# Makefile for Modular 64-bit Kernel (M1 Mac)

ASM = nasm
CC = clang
LD = x86_64-elf-ld

# 1. Flags
ASMFLAGS = -f elf64
# Note the -Isrc/include to find our types.h
CFLAGS = -target x86_64-pc-none-elf -ffreestanding -mno-red-zone -m64 \
         -mno-sse -mno-sse2 -mno-avx -mno-80387 -msoft-float \
         -Isrc/include \
         -O2 -Wall -Wextra

LDFLAGS = -m elf_x86_64 -T linker.ld

# 2. Automatically find sources
SRC_ASM = boot.asm
# Finds all .c files in src/ and subdirectories
SRC_C = $(shell find src -name "*.c")

SRC_ASM_ALL = $(shell find . -name "*.asm")
OBJ = $(SRC_ASM_ALL:.asm=.o) $(SRC_C:.c=.o)

# Update the assembly rule to handle multiple files
%.o: %.asm
	$(ASM) $(ASMFLAGS) $< -o $@

# 3. Targets

all: kernel.bin

# Compile Assembly
boot.o: $(SRC_ASM)
	$(ASM) $(ASMFLAGS) $(SRC_ASM) -o boot.o

# Compile C files (Pattern Rule)
# This allows us to compile src/kernel.c and src/drivers/vga.c automatically
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link
kernel.bin: $(OBJ)
	$(LD) $(LDFLAGS) -o kernel.bin $(OBJ)

# Limine Tool
limine/limine:
	$(CC) -O2 limine/limine.c -o limine/limine

# Create ISO
iso: kernel.bin limine/limine
	mkdir -p iso_root/boot
	mkdir -p iso_root/EFI/BOOT
	cp kernel.bin iso_root/
	cp limine.conf iso_root/
	cp limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/
	cp limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o os.iso
	./limine/limine bios-install os.iso

run: iso
	qemu-system-x86_64 -cdrom os.iso

clean:
	rm -f *.o src/*.o src/drivers/*.o *.bin *.iso
	rm -rf iso_root