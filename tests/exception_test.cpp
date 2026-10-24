#include <cstdint>
#include "tests.hpp"
#include "idt.hpp"

// isr.asm で定義される ISR スタブ関数のテーブル (本番の IDT が指している先)
extern "C" uint64_t isr_stubs[];

// 0 除算 (#DE) を起こす関数と、テスト中だけ #DE のベクタに差し込むハンドラ。
// 本番のハンドラ (isr_common_handler) は停止して戻らないので、テストの間だけ
// IDT の 0 番をこちらに差し替え、終わったら元に戻す。
//
// test_divide_by_zero は #DE が起きたら 1、起きなかったら 0 を返す:
//   - #DE が起きた: ハンドラが eax = 1 にして resume から再開させる
//   - 起きなかった: div の次の xor eax, eax を通って 0 になる
// 再開位置をラベルで固定するため、C++ ではなくアセンブリで書く
// (C++ だと 1/0 がコンパイル時に畳まれたり、div 以降の位置が決まらない)。
// rax / rcx / rdx は呼び出し側保存なので、普通の関数として呼んでよい。
extern "C" int test_divide_by_zero();
extern "C" void test_divide_error_handler();

asm(R"(
    .pushsection .text
    .intel_syntax noprefix
    .global test_divide_by_zero
    .global test_divide_error_handler
# div r/m32 は EDX:EAX (64 ビット) をオペランドで割り、商を EAX、余りを EDX に入れる。
# ここでは 1 / 0 を計算させる。
test_divide_by_zero:
    xor edx, edx    # 割られる数の上位 32 ビット = 0
    mov eax, 1      # 割られる数の下位 32 ビット = 1  (EDX:EAX = 1)
    xor ecx, ecx    # 割る数 = 0
    div ecx         # 1 / 0 → #DE
    xor eax, eax    # #DE が起きなかったときだけここを通る (戻り値 0)
test_divide_by_zero_resume:
    ret

# #DE はエラーコードを積まないので、[rsp] が戻り先の RIP
test_divide_error_handler:
    lea rax, [rip + test_divide_by_zero_resume]
    mov [rsp], rax
    mov eax, 1
    iretq
    .att_syntax prefix
    .popsection
)");

namespace tests
{
namespace
{

constexpr uint8_t kDivideErrorVector = 0; // #DE

uint64_t handler_address(const idt::InterruptDescriptorTableEntry &entry)
{
    return static_cast<uint64_t>(entry.offset_low) | (static_cast<uint64_t>(entry.offset_middle) << 16) |
           (static_cast<uint64_t>(entry.offset_high) << 32);
}

void set_handler_address(idt::InterruptDescriptorTableEntry &entry, uint64_t handler)
{
    entry.offset_low    = handler & 0xFFFF;
    entry.offset_middle = (handler >> 16) & 0xFFFF;
    entry.offset_high   = (handler >> 32) & 0xFFFFFFFF;
}

} // namespace

// 意図的に 0 除算 (#DE) を発生させ、IDT の 0 番に届くことを確認する。
// 本番の IDT が 0 番に isr_stub_0 を登録していることも合わせて確認する。
bool division_by_zero(IConsole *console)
{
    // 今ロードされている IDT を読む (InterruptDescriptorTable は外に公開していないため)
    idt::idt_ptr_t idtr{};
    asm volatile("sidt %0" : "=m"(idtr));
    auto &entry = reinterpret_cast<idt::InterruptDescriptorTableEntry *>(idtr.base)[kDivideErrorVector];

    const uint64_t original = handler_address(entry);
    const bool wired        = original == isr_stubs[kDivideErrorVector];

    // 差し替えている間に割り込みが入らないよう IF を落とし、終わったら元の RFLAGS に戻す
    uint64_t rflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(rflags) : : "memory");
    set_handler_address(entry, reinterpret_cast<uint64_t>(&test_divide_error_handler));
    const bool raised = test_divide_by_zero() == 1;
    set_handler_address(entry, original);
    asm volatile("push %0; popfq" : : "r"(rflags) : "memory", "cc");

    console->printf("[EXC] vector 0 -> isr_stub_0: %s, #DE raised: %s\n", wired ? "yes" : "no", raised ? "yes" : "no");
    return wired && raised;
}

} // namespace tests
