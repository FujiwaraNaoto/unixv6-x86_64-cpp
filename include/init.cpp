
#include "init.hpp"
#include "process.hpp"
#include "file.hpp"
#include "vga.hpp"

namespace Init
{
    void init_func(void *){
        File *console_in  = FileSystem::file_open_console(true, false);
        File *console_out = FileSystem::file_open_console(false, true);
        if (console_in != nullptr && console_out != nullptr)
        {
            vga::vga->puts("[ECHO] type something:\n> ");
            while (1)
            {
                uint8_t c = 0;
                if (FileSystem::file_read(*console_in, &c, 1))
                {
                    (void)FileSystem::file_write(*console_out, &c, 1);
                    if (c == '\n')
                    {
                        vga::vga->puts("> ");
                    }
                }
            }
        }
    }
    
}
