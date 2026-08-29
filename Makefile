CC       = g++
LD       = ld
NASM     = nasm
GRUB     = grub-mkrescue
QEMU     = qemu-system-x86_64

# -nostdinc で標準インクルードを切った上で、libstdc++ の C++ ヘッダ
# (<array> など) を使うために必要なパスだけを -isystem で足す。
# バージョン/ターゲットはコンパイラから取得してハードコードを避ける。
GCC_VER    := $(shell $(CC) -dumpversion)
GCC_TRIPLE := $(shell $(CC) -dumpmachine)
STD_INC    = -isystem /usr/include/c++/$(GCC_VER) \
             -isystem /usr/include/$(GCC_TRIPLE)/c++/$(GCC_VER) \
             -isystem /usr/lib/gcc/$(GCC_TRIPLE)/$(GCC_VER)/include \
             -isystem /usr/include/$(GCC_TRIPLE) \
             -isystem /usr/include

# NOTE: ubuntuのg++はデフォルトで --enable-default-pie が有効になっている．-mcmodel=kernelと競合するため、-fno-pie を明示的に指定する必要がある。
CFLAGS   = -m64 -std=c++20 -g \
           -ffreestanding -fno-stack-protector -fno-builtin \
           -fno-exceptions -fno-rtti \
           -nostdlib -nostdinc \
           -Wall -Wextra -O2 \
		   -mcmodel=kernel	\
           -fno-pic -fno-pie \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -Iinclude $(STD_INC)

LDFLAGS  = -T kernel.ld -nostdlib -z max-page-size=0x1000
NASMFLAGS = -f elf64 -Iinclude

ASM_SRC  = boot/boot.asm io/io.asm interrupt/isr.asm interrupt/helper.asm boot/switch.asm syscall/syscall_entry.asm syscall/helper.asm user/usermode_entry.asm syscall/fork_ret.asm include/gdt_helper.asm
CPP_SRC  = kernel/main.cpp \
           $(wildcard include/*.cpp)

OBJ_DIR  = build
ASM_OBJ  = $(ASM_SRC:%.asm=$(OBJ_DIR)/%.o)
CPP_OBJ  = $(CPP_SRC:%.cpp=$(OBJ_DIR)/%.o)
OBJS     = $(ASM_OBJ) $(CPP_OBJ)

KERNEL   = $(OBJ_DIR)/kernel.elf
ISO      = unixv6.iso


fs.img :
	dd if=/dev/zero of=fs.img bs=512 count=2048
	echo "HELLO VIRTIO BLOCK DEVICE" | dd of=fs.img conv=notrunc

# 共通フラグ
QEMU_COMMON    = -cdrom $(ISO) -boot d -m 128M -no-reboot -no-shutdown  -d int,cpu_reset -D qemu.log \
				  -drive file=fs.img,format=raw,if=none,id=disk0 \
				  -device virtio-blk-pci,drive=disk0,disable-modern=on



# VSCode 等のターミナルに直結 (シリアル = 標準入出力)。終了は Ctrl-A X
QEMU_TERM      = $(QEMU_COMMON) -nographic
# 別ウィンドウ表示 (GTK)。要 DISPLAY。シリアルは標準入出力(端末)にも出す
QEMU_GUI       = $(QEMU_COMMON) -display gtk -serial file:serial.log

QEMU_GDB_FLAGS = $(QEMU_TERM) -s -S

.PHONY: all iso run run-vscode run-gui run-gdb clean

all: $(KERNEL)
iso: $(ISO)

# 既定: ターミナル直結 (VSCode 統合ターミナルでもそのまま出る)
run: run-vscode

run-vscode: $(ISO)
	$(QEMU) $(QEMU_TERM)

run-gui: $(ISO)
	$(QEMU) $(QEMU_GUI)

run-gdb: $(ISO)
	$(QEMU) $(QEMU_GDB_FLAGS)

# asm/cpp とも build/ 以下にソースのディレクトリ構造をそのまま掘って出力する。
# (ベース名だけにすると interrupt/helper.asm と syscall/helper.asm のように
#  別ディレクトリの同名ファイルが同じ .o に潰れてシンボルが消える)
$(OBJ_DIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(NASM) $(NASMFLAGS) -o $@ $<

# NASM の %include 依存は Make からは見えないので、明示的に依存を張っておく。
# (これが無いと .inc を書き換えても再アセンブルされず、古いセレクタ値が残る)
# 注意: この行は `all:` より後に置くこと。レシピの無いルールでも「最初のターゲット」に
#       なりうるため、上の方に書くと make の既定ゴールが .o に奪われる。
$(ASM_OBJ): include/gdt_selectors.inc

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
	@echo 'set gfxpayload=text'                  >> iso/boot/grub/grub.cfg
	@echo 'menuentry "UnixV6 x86-64 C++ Ph1" {' >> iso/boot/grub/grub.cfg
	@echo '  multiboot2 /boot/kernel.elf'        >> iso/boot/grub/grub.cfg
	@echo '  boot'                               >> iso/boot/grub/grub.cfg
	@echo '}'                                    >> iso/boot/grub/grub.cfg
	$(GRUB) -o $@ iso
	@echo ">>> ISO ready: $@"

clean:
	rm -rf $(OBJ_DIR) iso $(ISO) qemu.log serial.log

format:
	@echo "[format] Running clang-format..."
	@find . -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i
	@echo "[format] Done."
