#ifndef ASM_PROCESSOR_HPP
#define ASM_PROCESSOR_HPP

// ビジーウェイトのループ 1 周ごとに呼ぶ
// "memory" クロバーがあるので、ループの条件に使うメモリはループの外へ巻き上げられず、毎回読み直される。
inline void cpu_relax()
{
    asm volatile("pause" ::: "memory");
}

#endif // ASM_PROCESSOR_HPP
