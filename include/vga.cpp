#include "vga.hpp"
#include "io.hpp"

// stdarg: __builtin で直接実装
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,l)   __builtin_va_arg(v,l)

namespace vga {

constexpr size_t   WIDTH  = 80;
constexpr size_t   HEIGHT = 25;
constexpr uint32_t ADDR   = 0xB8000;

static size_t  row_  = 0;
static size_t  col_  = 0;
static uint8_t attr_ = 0;

static constexpr uint8_t make_attr(Color fg, Color bg) {
    return (uint8_t)(((uint8_t)bg << 4) | ((uint8_t)fg & 0x0F));
}
static constexpr uint16_t make_entry(char c, uint8_t attr) {
    return (uint16_t)((uint8_t)c) | ((uint16_t)attr << 8);
}
static volatile uint16_t *buf() {
    return reinterpret_cast<volatile uint16_t *>(ADDR);
}

static void move_cursor() {
    uint16_t pos = (uint16_t)(row_ * WIDTH + col_);
    io::outb(0x3D4, 0x0F); io::outb(0x3D5, (uint8_t)(pos & 0xFF));
    io::outb(0x3D4, 0x0E); io::outb(0x3D5, (uint8_t)(pos >> 8));
}

static void scroll() {
    auto b = buf();
    for (size_t r = 1; r < HEIGHT; r++)
        for (size_t c = 0; c < WIDTH; c++)
            b[(r-1)*WIDTH + c] = b[r*WIDTH + c];
    for (size_t c = 0; c < WIDTH; c++)
        b[(HEIGHT-1)*WIDTH + c] = make_entry(' ', attr_);
    row_ = HEIGHT - 1;
}

void init() {
    attr_ = make_attr(Color::LightGrey, Color::Black);
    clear();
}

void set_color(Color fg, Color bg) {
    attr_ = make_attr(fg, bg);
}

void clear() {
    auto b = buf();
    for (size_t i = 0; i < WIDTH * HEIGHT; i++)
        b[i] = make_entry(' ', attr_);
    row_ = col_ = 0;
    move_cursor();
}

void putchar(char c) {
    auto b = buf();
    if (c == '\n') {
        col_ = 0;
        if (++row_ >= HEIGHT) scroll();
    } else if (c == '\r') {
        col_ = 0;
    } else if (c == '\t') {
        col_ = (col_ + 8) & ~7u;
        if (col_ >= WIDTH) { col_ = 0; if (++row_ >= HEIGHT) scroll(); }
    } else {
        b[row_ * WIDTH + col_] = make_entry(c, attr_);
        if (++col_ >= WIDTH) { col_ = 0; if (++row_ >= HEIGHT) scroll(); }
    }
    move_cursor();
}

void puts(const char *s) {
    while (*s) putchar(*s++);
}

static void print_uint(unsigned long long n, int base, int width, char pad) {
    static const char digits[] = "0123456789abcdef";
    char tmp[64];
    int i = 0;
    if (n == 0) { tmp[i++] = '0'; }
    else { while (n) { tmp[i++] = digits[n % base]; n /= base; } }
    while (i < width) tmp[i++] = pad;
    while (i--) putchar(tmp[i]);
}

void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { putchar(*fmt); continue; }
        fmt++;
        int width = 0; char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width*10 + (*fmt-'0'); fmt++; }
        switch (*fmt) {
            case 'd': { auto v = va_arg(ap, long long); if (v<0){putchar('-');v=-v;} print_uint((unsigned long long)v, 10, width, pad); break; }
            case 'u': print_uint(va_arg(ap, unsigned long long), 10, width, pad); break;
            case 'x': print_uint(va_arg(ap, unsigned long long), 16, width, pad); break;
            case 'p': puts("0x"); print_uint((unsigned long long)va_arg(ap, void*), 16, 16, '0'); break;
            case 's': puts(va_arg(ap, const char *)); break;
            case 'c': putchar((char)va_arg(ap, int)); break;
            case '%': putchar('%'); break;
        }
    }
    va_end(ap);
}

} // namespace vga
