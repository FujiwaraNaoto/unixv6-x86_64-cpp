
#include "init.hpp"
#include "process.hpp"
#include "file.hpp"
#include "console.hpp"

constexpr int BUF_SIZE = 1 << 10;
namespace Init
{
void init_func(void *arg)
{
    IConsole *console = (arg != nullptr) ? static_cast<IConsole *>(arg) : null_console;
    File *console_in  = FileSystem::file_open_console(true, false);
    File *console_out = FileSystem::file_open_console(false, true);

    uint8_t buf[BUF_SIZE];
    if (console_in != nullptr && console_out != nullptr)
    {
        console->puts("[ECHO] type something:\n");
        while (1) // ずっとinit processにするためのループ
        {
            console->puts("> ");

            uint8_t c = 0;
            int idx   = 0;
            while (1)
            {
                if (FileSystem::file_read(*console_in, &c, 1))
                {
                    // キーボード表示用
                    (void)FileSystem::file_write(*console_out, &c, 1);
                    buf[idx++] = c;
                    if (c == '\n' || idx == BUF_SIZE - 1)
                    {
                        buf[idx] = '\0';
                        break;
                    }
                }
            }
            FileSystem::file_write(*console_out, buf, idx);
        }
    }
}

} // namespace Init
