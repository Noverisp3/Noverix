ASM=nasm
CC=clang
LD=ld.bfd
OBJCOPY=objcopy
CFLAGS=-ffreestanding -m32 -Wall -Wextra -Werror -nostdlib -fno-pie -c
LDFLAGS=-m elf_i386 -T linker.ld -Map kernel.map

BUILD_DIR=build

SOURCE_DIRS=kernel kernel/drivers kernel/cpu kernel/memory kernel/acpi kernel/apic kernel/scheduler
vpath %.c $(SOURCE_DIRS)
vpath %.S $(SOURCE_DIRS)

KERNEL_OBJS = \
	$(BUILD_DIR)/entry.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/screen.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/ata.o \
	$(BUILD_DIR)/nvfs.o \
	$(BUILD_DIR)/gdt.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/interrupt.o \
	$(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/pfa.o \
	$(BUILD_DIR)/paging.o \
	$(BUILD_DIR)/heap.o \
	$(BUILD_DIR)/graphics.o \
	$(BUILD_DIR)/elf.o \
	$(BUILD_DIR)/lib.o \
	$(BUILD_DIR)/rsdp.o \
	$(BUILD_DIR)/madt.o \
	$(BUILD_DIR)/lapic.o \
	$(BUILD_DIR)/ioapic.o \
	$(BUILD_DIR)/ap_startup.o \
	$(BUILD_DIR)/ap_trampoline.o \
	$(BUILD_DIR)/scheduler.o

.PHONY: all clean run run-qemu iso

all: $(BUILD_DIR)/os-image.bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/entry.o: kernel/entry.S | $(BUILD_DIR)
	$(CC) -m32 -c $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/cpu/interrupt.S | $(BUILD_DIR)
	$(CC) -m32 -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/ap_trampoline.bin: boot/ap_trampoline.asm | $(BUILD_DIR)
	$(ASM) -f bin $< -o $@

$(BUILD_DIR)/ap_trampoline.o: $(BUILD_DIR)/ap_trampoline.bin | $(BUILD_DIR)
	$(OBJCOPY) -I binary -O elf32-i386 -B i386 $< $@

$(BUILD_DIR)/kernel.elf: $(KERNEL_OBJS) | $(BUILD_DIR)
	$(LD) $(LDFLAGS) $(KERNEL_OBJS) -o $@

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf | $(BUILD_DIR)
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/bootloader.bin: boot/bootloader.asm $(BUILD_DIR)/kernel.bin | $(BUILD_DIR)
	kernel_size=$$(stat -c%s $(BUILD_DIR)/kernel.bin); \
	sectors=$$(( (kernel_size + 511) / 512 )); \
	$(ASM) -f bin boot/bootloader.asm -dKERNEL_SECTORS=$$sectors -o $@

$(BUILD_DIR)/os-image.bin: $(BUILD_DIR)/bootloader.bin $(BUILD_DIR)/kernel.bin | $(BUILD_DIR)
	cat $^ > $@
	truncate -s 1474560 $@

clean:
	rm -rf $(BUILD_DIR) rootfs nvfs_disk.img os-image.iso noverix.img

nvfs_disk.img: tools/mknvfs.py rootfs/triangle.elf
	python3 tools/mknvfs.py $@ rootfs

rootfs/triangle.elf: tests/triangle.c tests/noverix.h tests/app.ld | $(BUILD_DIR)
	mkdir -p rootfs
	$(CC) -m32 -ffreestanding -fno-pie -fno-pic -c tests/triangle.c -o $(BUILD_DIR)/triangle.o
	$(LD) -m elf_i386 -no-pie -T tests/app.ld $(BUILD_DIR)/triangle.o -o $@

$(BUILD_DIR)/os-image.iso: $(BUILD_DIR)/os-image.bin
	xorriso -as mkisofs -b os-image.bin -no-emul-boot -boot-load-size 4 \
	  -o $@ $(BUILD_DIR)

noverix.img: $(BUILD_DIR)/os-image.bin nvfs_disk.img
	cp $(BUILD_DIR)/os-image.bin $@
	cat nvfs_disk.img >> $@
	@echo "Created $@ — dd to USB: sudo dd if=$@ of=/dev/sdX bs=512"

run-qemu: $(BUILD_DIR)/os-image.bin nvfs_disk.img
	qemu-system-x86_64 -vga std -boot order=a -drive format=raw,file=$<,if=floppy -drive file=nvfs_disk.img,format=raw,if=none,id=ata0 -device ide-hd,drive=ata0 -m 32 -smp 2 -serial mon:stdio

run-qemu-iso: $(BUILD_DIR)/os-image.iso nvfs_disk.img
	qemu-system-x86_64 -vga std -boot order=d -cdrom $(BUILD_DIR)/os-image.iso \
	  -drive file=nvfs_disk.img,format=raw,if=none,id=ata0 \
	  -device ide-hd,drive=ata0 -smp 2 -m 32

run-qemu-nrx: noverix.img
	qemu-system-x86_64 -vga std -smp 2 -boot order=c \
	  -drive file=noverix.img,format=raw,if=none,id=ata0 \
	  -device ide-hd,drive=ata0 -m 32 -serial mon:stdio

run: $(BUILD_DIR)/os-image.bin
	bochs -q -f bochsrc.bxrc

iso: $(BUILD_DIR)/os-image.iso
