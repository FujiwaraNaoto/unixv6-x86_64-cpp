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

# ─── ビルド構成の切り替え (通常 / テスト) ─────────────────────────
# TESTS=1 のとき tests/ 以下を一緒にコンパイルし、-DENABLE_TESTS で
# kernel_main から tests::run_all() を呼ぶ。既定 (TESTS=0) では tests/ は
# 1 つもコンパイルされず、カーネルは初期化して hlt ループに入るだけになる。
#
#   make         … 通常カーネル       build/kernel/ + unixv6.iso
#   make tests   … テスト入りを起動   build/tests/  + unixv6-tests.iso
#
# 生成物のディレクトリと ISO 名を分けてあるので、両者を行き来しても
# make clean は不要 (フラグの違う .o が混ざらない)。
TESTS ?= 0

ifeq ($(TESTS),1)
  BUILD_NAME = tests
  TEST_FLAGS = -DENABLE_TESTS -Itests
  TEST_SRC   = $(wildcard tests/*.cpp)
  ISO        = unixv6-tests.iso
  GRUB_TITLE = UnixV6 x86-64 C++ (tests)
else
  BUILD_NAME = kernel
  TEST_FLAGS =
  TEST_SRC   =
  ISO        = unixv6.iso
  GRUB_TITLE = UnixV6 x86-64 C++ Ph1
endif

# NOTE: ubuntuのg++はデフォルトで --enable-default-pie が有効になっている．-mcmodel=kernelと競合するため、-fno-pie を明示的に指定する必要がある。
CFLAGS   = -m64 -std=c++20 -g \
           -ffreestanding -fno-stack-protector -fno-builtin \
           -fno-exceptions -fno-rtti \
           -nostdlib -nostdinc \
           -Wall -Wextra -O2 \
		   -mcmodel=kernel	\
           -fno-pic -fno-pie \
           -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
		   -MMD -MP \
           -Iinclude $(TEST_FLAGS) $(STD_INC)

LDFLAGS  = -T kernel.ld -nostdlib -z max-page-size=0x1000
NASMFLAGS = -f elf64 -Iinclude

ASM_SRC  = boot/boot.asm io/io.asm interrupt/isr.asm interrupt/helper.asm boot/switch.asm syscall/syscall_entry.asm syscall/helper.asm user/usermode_entry.asm syscall/fork_ret.asm include/gdt_helper.asm
CPP_SRC  = kernel/main.cpp \
           $(wildcard include/*.cpp) \
           $(TEST_SRC)

OBJ_DIR  = build/$(BUILD_NAME)
ISO_DIR  = $(OBJ_DIR)/iso
ASM_OBJ  = $(ASM_SRC:%.asm=$(OBJ_DIR)/%.o)
CPP_OBJ  = $(CPP_SRC:%.cpp=$(OBJ_DIR)/%.o)
OBJS     = $(ASM_OBJ) $(CPP_OBJ)
DEPS     = $(CPP_OBJ:.o=.d)

KERNEL   = $(OBJ_DIR)/kernel.elf


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

.PHONY: all iso run run-vscode run-gui run-gdb tests tests-build tests-gui tests-gdb clean format

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

# ─── テスト構成 (TESTS=1 で自分を呼び直す) ───────────────────────
# NOTE: tests/ というディレクトリが存在するので、.PHONY の宣言は必須。
#       (無いと make が「tests は最新」と判断してレシピを実行しない)
tests: fs.img
	@$(MAKE) --no-print-directory TESTS=1 run

tests-build:
	@$(MAKE) --no-print-directory TESTS=1 all

tests-gui: fs.img
	@$(MAKE) --no-print-directory TESTS=1 run-gui

tests-gdb: fs.img
	@$(MAKE) --no-print-directory TESTS=1 run-gdb

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

# ISO のステージングも構成ごとに分ける (通常/テストで中身が違うため)
$(ISO): $(KERNEL)
	mkdir -p $(ISO_DIR)/boot/grub
	cp $(KERNEL) $(ISO_DIR)/boot/kernel.elf
	@echo 'set timeout=0'                       >  $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'set default=0'                       >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'set gfxpayload=text'                 >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo 'menuentry "$(GRUB_TITLE)" {'         >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '  multiboot2 /boot/kernel.elf'       >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '  boot'                              >> $(ISO_DIR)/boot/grub/grub.cfg
	@echo '}'                                   >> $(ISO_DIR)/boot/grub/grub.cfg
	$(GRUB) -o $@ $(ISO_DIR)
	@echo ">>> ISO ready: $@"

clean:
	rm -rf build iso unixv6.iso unixv6-tests.iso qemu.log serial.log

format:
	@echo "[format] Running clang-format..."
	@find . -type f \( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0 | xargs -0 clang-format -i
	@echo "[format] Done."

# コンパイル時に -MMD -MP で生成したヘッダ依存 (.d) を取り込む。
# ヘッダを変えたとき、それをインクルードしている .cpp も再コンパイルされる。
# 先頭の - で、初回ビルドなど .d がまだ無いときもエラーにしない。
-include $(DEPS)
