
#include "pic.hpp"
#include "io.hpp"


namespace
{
const int PIC1_COMMAND = 0x20;
const int PIC1_DATA    = 0x21;
const int PIC2_COMMAND = 0xA0;
const int PIC2_DATA    = 0xA1;
const int PIC_EOI      = 0x20;

const int PIT_CHANNEL0 = 0x40;
const int PIT_COMMAND  = 0x43;
// 8253/8254 PIT input clock: 14.31818MHz / 12 = 1.193182MHz (established PC spec).
// https://f.osdev.org/viewtopic.php?t=15503
// https://en.wikipedia.org/wiki/Color_burst
const uint32_t PIT_FREQUENCY = 1193182;

// ─── 8259A の初期化コマンドワード (ICW) ──────────────────────────
// ICW1 をコマンドポートに書くと、PIC は ICW2 → ICW3 → ICW4 の順で
// データポートへの書き込みを待つ状態になる。

// ICW1: 初期化の開始を伝える
enum class Icw1 : uint8_t
{
    Icw4Needed     = 0x01, // この後 ICW4 も送る
    Single         = 0x02, // PIC 1 個だけで使う (0=カスケードする)
    Interval4      = 0x04, // 8080/8085 時代の call address interval。8086 では使わない
    LevelTriggered = 0x08, // 1=レベルトリガ, 0=エッジトリガ
    Initialize     = 0x10, // 初期化開始。ICW1 には必ず立てる
};

// ICW4: 8086/88 で動かすための設定。
// AutoEoi は使わない (このカーネルは send_eoi() で手動送信する)。
enum class Icw4 : uint8_t
{
    Mode8086           = 0x01, // 1=8086/88 モード, 0=MCS-80/85 モード
    AutoEoi            = 0x02, // 1=割り込み受付時に自動で EOI
    BufferedSlave      = 0x08, // バッファモード (スレーブ側)
    BufferedMaster     = 0x0C, // バッファモード (マスタ側)
    SpecialFullyNested = 0x10, // 多段カスケード用。2 個構成では不要
};

constexpr Icw1 operator|(Icw1 a, Icw1 b)
{
    return static_cast<Icw1>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr Icw4 operator|(Icw4 a, Icw4 b)
{
    return static_cast<Icw4>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

// enum の中身のバイトを取り出す (C++23 の std::to_underlying 相当)
template <typename E>
constexpr uint8_t to_byte(E value)
{
    return static_cast<uint8_t>(value);
}

// ICW3: カスケードの結線を伝える。マスタとスレーブで値の意味が違う。
//   マスタ  : スレーブが繋がっている IRQ 線のビットマスク
//   スレーブ: 自分がマスタのどの IRQ 線に繋がっているかの番号そのもの
constexpr uint8_t CASCADE_IRQ         = 2;                // PC/AT の配線では IRQ2 でつながっている
constexpr uint8_t MASTER_CASCADE_MASK = 1 << CASCADE_IRQ; // 0x04
constexpr uint8_t SLAVE_CASCADE_ID    = CASCADE_IRQ;      // 0x02

// ─── 8253/8254 PIT のコントロールワード ──────────────────────────
// PIT_COMMAND に 1 バイト書いて、どのカウンタをどのモードで動かすかを決める。

// bit 7-6: どのカウンタ (チャネル) の設定か
enum class PitChannel : uint8_t
{
    Channel0 = 0b00, // IRQ0 につながっている。タイマ割り込み用
    Channel1 = 0b01, // 昔の DRAM リフレッシュ用。今は使わない
    Channel2 = 0b10, // PC スピーカー用
    ReadBack = 0b11, // 8254 の読み出しコマンド (カウンタの設定ではない)
};

// bit 5-4: カウンタ値をどう読み書きするか
enum class PitAccess : uint8_t
{
    LatchCount      = 0b00, // 現在値をラッチして読む
    LowByteOnly     = 0b01,
    HighByteOnly    = 0b10,
    LowThenHighByte = 0b11, // 下位バイト → 上位バイトの順に 2 回書く
};

// bit 3-1: 動作モード
enum class PitMode : uint8_t
{
    InterruptOnTerminalCount     = 0b000,
    HardwareRetriggerableOneShot = 0b001,
    RateGenerator                = 0b010, // 指定間隔ごとに 1 パルス
    SquareWave                   = 0b011, // 方形波。周期的なタイマ割り込みに使う
    SoftwareTriggeredStrobe      = 0b100,
    HardwareTriggeredStrobe      = 0b101,
};

// bit 0: カウンタの数値の形式
enum class PitCounterFormat : uint8_t
{
    Binary = 0, // 16bit バイナリ
    Bcd    = 1, // 4 桁の BCD
};

struct PitCommand
{
    // コントロールワードのビット構成
    // bit:  7-6   5-4   3-1    0
    //       SC    RW    Mode   BCD
    // 0x36 = 00 11 011 0 → チャネル0, 下位→上位の順で書き込み, モード3 (方形波), バイナリ
    PitChannel channel              : 2 = PitChannel::Channel0;
    PitAccess access                : 2 = PitAccess::LowThenHighByte;
    PitMode mode                    : 3 = PitMode::SquareWave;
    PitCounterFormat counter_format : 1 = PitCounterFormat::Binary;

    constexpr operator uint8_t() const
    {
        return (static_cast<uint8_t>(channel) << 6) | (static_cast<uint8_t>(access) << 4) |
               (static_cast<uint8_t>(mode) << 1) | static_cast<uint8_t>(counter_format);
    }
};

// これまで直接書いていた値と同じであることを、ビルド時に確かめる
static_assert(to_byte(Icw1::Initialize | Icw1::Icw4Needed) == 0x11, "ICW1");
static_assert(MASTER_CASCADE_MASK == 0x04, "ICW3 (master)");
static_assert(SLAVE_CASCADE_ID == 0x02, "ICW3 (slave)");
static_assert(to_byte(Icw4::Mode8086) == 0x01, "ICW4");
static_assert(static_cast<uint8_t>(PitCommand{.channel        = PitChannel::Channel0,
                                              .access         = PitAccess::LowThenHighByte,
                                              .mode           = PitMode::SquareWave,
                                              .counter_format = PitCounterFormat::Binary}) == 0x36,
              "PIT control word");
} // namespace

namespace pic
{
// pic_init
void InitializePIC(uint8_t offset1, uint8_t offset2)
{
    // see https://wiki.osdev.org/8259_PIC
    // reserve 2 bytes for master and slave PIC mask
    uint8_t m1 = io::inb(PIC1_DATA);
    uint8_t m2 = io::inb(PIC2_DATA);

    io::outb(PIC1_COMMAND, Icw1::Initialize | Icw1::Icw4Needed);
    io::io_wait();
    io::outb(PIC2_COMMAND, Icw1::Initialize | Icw1::Icw4Needed);
    io::io_wait();
    io::outb(PIC1_DATA, offset1);
    io::io_wait();
    io::outb(PIC2_DATA, offset2);
    io::io_wait();
    io::outb(PIC1_DATA, MASTER_CASCADE_MASK);
    io::io_wait();
    io::outb(PIC2_DATA, SLAVE_CASCADE_ID);
    io::io_wait();
    io::outb(PIC1_DATA, Icw4::Mode8086);
    io::io_wait();
    io::outb(PIC2_DATA, Icw4::Mode8086);
    io::io_wait();

    // restore saved masks.
    io::outb(PIC1_DATA, m1);
    io::outb(PIC2_DATA, m2);
}

void InitializePIT(uint32_t hz)
{
    io::outb(PIT_COMMAND,
             PitCommand{.channel        = PitChannel::Channel0,
                        .access         = PitAccess::LowThenHighByte,
                        .mode           = PitMode::SquareWave,
                        .counter_format = PitCounterFormat::Binary});

    uint16_t divisor = PIT_FREQUENCY / hz;

    // The 16-bit divisor must be written as two 8-bit writes (low byte first,
    // then high byte). The PIT data port is only 8 bits wide, and the
    // lobyte/hibyte access mode set in the command byte above tells the chip
    // to latch the two consecutive writes in that order. A single 16-bit OUT
    // would not work: the x86 bus splits it into byte writes to ports 0x40
    // and 0x41, sending the high byte to channel 1 instead of channel 0.
    io::outb(PIT_CHANNEL0, divisor & 0xFF);
    io::outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

void mask_irq(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8)
        irq -= 8;
    io::outb(port, io::inb(port) | (1 << irq));
}

void unmask_irq(uint8_t irq)
{
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8)
        irq -= 8;
    io::outb(port, io::inb(port) & ~(1 << irq));
}

void send_eoi(uint8_t irq)
{
    // see reference: https://wiki.osdev.org/8259_PIC
    if (irq >= 8)
    {
        io::outb(PIC2_COMMAND, PIC_EOI);
    }
    io::outb(PIC1_COMMAND, PIC_EOI);
}
} // namespace pic
