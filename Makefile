ASM=nasm
CC=clang
LD=ld.bfd
OBJCOPY=objcopy
CFLAGS=-ffreestanding -m32 -Wall -Wextra -nostdlib -fno-pie -c
LDFLAGS=-m elf_i386 -T linker.ld -Map kernel.map

BUILD_DIR=build

SOURCE_DIRS=kernel kernel/drivers kernel/cpu kernel/memory
vpath %.c $(SOURCE_DIRS)
vpath %.S $(SOURCE_DIRS)

KERNEL_OBJS = \
	$(BUILD_DIR)/entry.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/screen.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/ata.o \
	$(BUILD_DIR)/fat16.o \
	$(BUILD_DIR)/gdt.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/interrupt.o \
	$(BUILD_DIR)/timer.o \
	$(BUILD_DIR)/pfa.o \
	$(BUILD_DIR)/paging.o

.PHONY: all clean run run-qemu

all: $(BUILD_DIR)/os-image.bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/entry.o: kernel/entry.S | $(BUILD_DIR)
	$(CC) -m32 -c $< -o $@

$(BUILD_DIR)/interrupt.o: kernel/cpu/interrupt.S | $(BUILD_DIR)
	$(CC) -m32 -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

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
	rm -rf $(BUILD_DIR)

run-qemu: $(BUILD_DIR)/os-image.bin
	qemu-system-x86_64 -drive format=raw,file=$<,if=floppy -drive file=disk.img,format=raw,if=none,id=ata0 -device ide-hd,drive=ata0 -m 32

run: $(BUILD_DIR)/os-image.bin
	bochs -q -f bochsrc.bxrc
