# XYZT-OS v6 — Pure Co-Presence, On Metal
#
# Build: make (requires aarch64-none-elf-gcc)
# Clean: make clean

CROSS = aarch64-none-elf-
CC = $(CROSS)gcc
AS = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

CFLAGS = -Wall -Wextra -O2 -ffreestanding -nostdlib -nostartfiles
CFLAGS += -mcpu=cortex-a53 -mgeneral-regs-only

ASM_SRCS = boot.S
C_SRCS = drivers.c kernel.c

ASM_OBJS = $(ASM_SRCS:.S=.o)
C_OBJS = $(C_SRCS:.c=.o)
OBJS = $(ASM_OBJS) $(C_OBJS)

TARGET = kernel8

all: $(TARGET).img

$(TARGET).img: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "====================================="
	@echo "  XYZT-OS v6 built: kernel8.img"
	@echo "  Copy to SD card boot partition"
	@echo "====================================="

$(TARGET).elf: $(OBJS) link.ld
	$(LD) -nostdlib $(OBJS) -T link.ld -o $@

boot.o: boot.S
	$(AS) $(CFLAGS) -c $< -o $@

%.o: %.c xyzt_os.h hw.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o *.elf *.img

.PHONY: all clean
