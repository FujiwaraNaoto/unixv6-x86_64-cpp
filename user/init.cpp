#include "user_init.hpp"
#include "user_syscall.hpp"
#include "command.hpp"

constexpr int kLineBufferSize = 1 << 10;
constexpr int kMaxArgs = 16;


// NOTE: ポインタで書くと.rodataに置かれてuserからアクセスしようとして保護違反となる
[[gnu::section(".user.rodata")]] static const char kEcho[] = "echo";
[[gnu::section(".user.rodata")]] static const char kCommandNotFound[] = "command not found \n";


[[noreturn, gnu::section(".user")]] void shell_main()
{
    const char prompt[]="> ";
    char line[kLineBufferSize];
    char *argv[kMaxArgs+1];
    int argc = 0;

    while(1){
        put(prompt,2);
        read_line(line,sizeof(line));
        argc = split(line,argv,kMaxArgs);
        if(argc==0) continue;

        if(strcmp(argv[0],kEcho)){
            for (int i = 1; i < argc; i++)
            {
                if (i > 1)
                    put_char(' ');
                put(argv[i], strlen(argv[i]));
            }
            put_char('\n');
            continue;

        }else{
            
        }
    }
}


[[gnu::section(".user")]] void init_main()
{
    while(1)
    {// ずっとinitでいるための無限ループ
        const long pid = user_syscall(SyscallNo::kFork,0,0,0);
        switch(pid){
            case 0:
                shell_main();
                break;
            case -1:
                //error
                continue;
            default:
                break;
        }
        int status = 0;

        while(user_syscall(SyscallNo::kWait,reinterpret_cast<long>(&status),0,0)!=pid){
            continue;
        }

    }// init
}
