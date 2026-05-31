ASM=nasm
CC=gcc
OBJCOPY=objcopy
CFLAGS=-ffreestanding -m32 -Wall -Wextra -nostdlib -fno-pie -c
LDFLAGS=-m32 -nostdlib -nostartfiles -fno-pie -no-pie -e _start
LDFLAGS+=-Wl,--image-base,0x1000
LDFLAGS+=-Wl,--file-alignment,0x200

BUILD_DIR=build

SOURCE_DIRS=kernel kernel/drivers kernel/cpu
vpath %.c $(SOURCE_DIRS)
vpath %.S $(SOURCE_DIRS)

KERNEL_OBJS = \
	$(BUILD_DIR)/entry.o \
	$(BUILD_DIR)/kernel.o \
	$(BUILD_DIR)/screen.o \
	$(BUILD_DIR)/keyboard.o \
	$(BUILD_DIR)/serial.o \
	$(BUILD_DIR)/gdt.o \
	$(BUILD_DIR)/idt.o \
	$(BUILD_DIR)/interrupt.o

KERNEL_SECTORS = $(shell powershell -noprofile -Command "if (Test-Path $(BUILD_DIR)/kernel.bin) { [math]::Ceiling((Get-Item $(BUILD_DIR)/kernel.bin).Length/512) } else { 64 }")

.PHONY: all clean run

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
	$(CC) $(LDFLAGS) $(KERNEL_OBJS) -o $@

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/kernel.elf | $(BUILD_DIR)
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/bootloader.bin: boot/bootloader.asm $(BUILD_DIR)/kernel.bin | $(BUILD_DIR)
	$(ASM) -f bin boot/bootloader.asm -dKERNEL_SECTORS=$(KERNEL_SECTORS) -o $@

$(BUILD_DIR)/os-image.bin: $(BUILD_DIR)/bootloader.bin $(BUILD_DIR)/kernel.bin | $(BUILD_DIR)
	powershell -File combine.ps1 -boot $(BUILD_DIR)/bootloader.bin -kernel $(BUILD_DIR)/kernel.bin -output $@

clean:
	rm -rf $(BUILD_DIR)

run: $(BUILD_DIR)/os-image.bin
	bochs -q -f bochsrc.bxrc
