// Minimal freestanding C++ runtime stubs.
//
// Having polymorphic globals (vga::vga, serial::serial, etc.) makes the
// compiler require C++ runtime symbols (libstdc++/libsupc++), which a
// -nostdlib kernel does not link. The kernel never "exits", so static
// destructor registration (__cxa_atexit) and operator delete are never
// actually needed; these empty implementations exist only to satisfy the
// linker.

#include <cstddef>

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
}

// operator delete referenced by the deleting destructor (D0) in the vtable of
// classes with a virtual destructor. These globals are never heap-allocated,
// so it is never called and an empty body is sufficient.
void operator delete(void *) noexcept { }
void operator delete(void *, std::size_t) noexcept { }
