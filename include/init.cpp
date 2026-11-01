
#include "init.hpp"
#include "process.hpp"
#include "file.hpp"
#include "console.hpp"

namespace Init
{
    void init_func(void *arg){
        IConsole *console = (arg != nullptr) ? static_cast<IConsole *>(arg) : null_console;
        File *console_in  = FileSystem::file_open_console(true, false);
        File *console_out = FileSystem::file_open_console(false, true);
        if (console_in != nullptr && console_out != nullptr)
        {
            console->puts("[ECHO] type something:\n> ");
            while (1)
            {
                uint8_t c = 0;
                if (FileSystem::file_read(*console_in, &c, 1))
                {
                    (void)FileSystem::file_write(*console_out, &c, 1);
                    if (c == '\n')
                    {
                        console->puts("> ");
                    }
                }
            }
        }
    }
    
}
