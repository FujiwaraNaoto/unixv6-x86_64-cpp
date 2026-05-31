CC       = g++
LD       = ld
NASM     = nasm
GRUB     = grub-mkrescue
QEMU     = qemu-system-x86_64

CFLAGS   = -m64 -std=c++17 \
           -ffreestanding -fno-stack-protector -fno-builtin \
           -fno-exceptions -fno-rtti \
           -nostdlib -nostdinc \
           -Wall -Wextra -O2 \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Iinclude

LDFLAGS  = -T kernel.ld -nostdlib -z max-page-size=0x1000
NASMFLAGS = -f elf64

ASM_SRC  = boot/boot.asm io/io.asm
CPP_SRC  = kernel/main.cpp \
           include/vga.cpp \
           include/serial.cpp

OBJ_DIR  = build
ASM_OBJ  = $(OBJ_DIR)/boot.o $(OBJ_DIR)/io.o
CPP_OBJ  = $(CPP_SRC:%.cpp=$(OBJ_DIR)/%.o)
OBJS     = $(ASM_OBJ) $(CPP_OBJ)

KERNEL   = $(OBJ_DIR)/kernel.elf
ISO      = unixv6.iso

QEMU_FLAGS     = -cdrom $(ISO) -boot d -m 128M -serial stdio \
                 -display gtk -no-reboot -no-shutdown
QEMU_GDB_FLAGS = $(QEMU_FLAGS) -s -S

.PHONY: all iso run run-gdb clean

all: $(KERNEL)
iso: $(ISO)

run: $(ISO)
	$(QEMU) $(QEMU_FLAGS)

run-gdb: $(ISO)
	$(QEMU) $(QEMU_GDB_FLAGS)

$(OBJ_DIR)/boot.o: boot/boot.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(OBJ_DIR)/io.o: io/io.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo ">>> build complete: $@"
	@size $@

$(ISO): $(KERNEL)
	mkdir -p iso/boot/grub
	cp $(KERNEL) iso/boot/kernel.elf
	@echo 'set timeout=0'                        >  iso/boot/grub/grub.cfg
	@echo 'set default=0'                        >> iso/boot/grub/grub.cfg
	@echo 'menuentry "UnixV6 x86-64 C++ Ph1" {' >> iso/boot/grub/grub.cfg
	@echo '  multiboot2 /boot/kernel.elf'        >> iso/boot/grub/grub.cfg
	@echo '  boot'                               >> iso/boot/grub/grub.cfg
	@echo '}'                                    >> iso/boot/grub/grub.cfg
	$(GRUB) -o $@ iso
	@echo ">>> ISO ready: $@"

clean:
	rm -rf $(OBJ_DIR) iso $(ISO)
