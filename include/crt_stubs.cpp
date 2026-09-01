// Minimal freestanding C/C++ runtime stubs.
//
// -ffreestanding でも、コンパイラは構造体コピーや配列の初期化に対して
// memset / memcpy / memmove の呼び出しを自由に生成してよいことになっている
// (C11 4.6 / GCC: "freestanding environments must provide memcpy, memmove,
//  memset and memcmp")。-nostdlib なので実体はカーネル側で持つ必要がある。
//
// Having polymorphic globals (vga::vga, serial::serial, etc.) makes the
// compiler require C++ runtime symbols (libstdc++/libsupc++), which a
// -nostdlib kernel does not link. The kernel never "exits", so static
// destructor registration (__cxa_atexit) and operator delete are never
// actually needed; these empty implementations exist only to satisfy the
// linker.

#include <cstddef>
#include <cstdint>

extern "C"
{

    // Fallback invoked if a pure virtual function (= 0) is ever actually called.
    // It should never happen in correct operation, but the linker requires the
    // symbol to exist.
    [[noreturn]] void __cxa_pure_virtual()
    {
        while (1)
            __asm__ volatile("hlt");
    }

    // Registration hook for destructors of static-storage objects. The kernel
    // never exits, so we register nothing and always report success
    // (i.e. no destructor ever runs).
    int __cxa_atexit(void (*)(void *), void *, void *)
    {
        return 0;
    }

    // Dummy symbol referenced as the third argument (DSO handle) of __cxa_atexit.
    void *__dso_handle = nullptr;

    // memset / memcpy は文字列命令で実装する。
    // C のループで書くと GCC の loop-distribute-patterns が「これは memset だ」と
    // 判断して memset 自身の呼び出しに置き換え、無限再帰になることがある。
    void *memset(void *dest, int value, std::size_t count)
    {
        void *d = dest;
        __asm__ volatile("rep stosb" : "+D"(d), "+c"(count) : "a"(static_cast<uint8_t>(value)) : "memory");
        return dest;
    }

    void *memcpy(void *dest, const void *src, std::size_t count)
    {
        void *d       = dest;
        const void *s = src;
        __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(count) : : "memory");
        return dest;
    }

    // IConsole::puts() の __builtin_strlen が -fno-builtin 下で呼び出しを生成する。
    std::size_t strlen(const char *s)
    {
        const char *p = s;
        while (*p != '\0')
        {
            p++;
        }
        return static_cast<std::size_t>(p - s);
    }

    // 領域が重なる場合も正しく動くコピー。
    // dest が src より後ろにあるときだけ後方から写す。
    void *memmove(void *dest, const void *src, std::size_t count)
    {
        if (dest < src)
        {
            return memcpy(dest, src, count);
        }
        auto *d       = static_cast<uint8_t *>(dest);
        const auto *s = static_cast<const uint8_t *>(src);
        for (std::size_t i = count; i > 0; i--)
        {
            d[i - 1] = s[i - 1];
        }
        return dest;
    }
}

// operator delete referenced by the deleting destructor (D0) in the vtable of
// classes with a virtual destructor. These globals are never heap-allocated,
// so it is never called and an empty body is sufficient.
void operator delete(void *) noexcept { }
void operator delete(void *, std::size_t) noexcept { }
