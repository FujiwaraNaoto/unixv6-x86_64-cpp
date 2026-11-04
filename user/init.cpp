#include "user_syscall.hpp"

constexpr int LINE_BUFFER_SIZE=1<<10;
// TODO: 一時的な動作確認用。キーボード入力を File 経由でそのまま画面に出す。シェルができたら消す。
[[gnu::section(".user")]] void init_main(){
    const char prompt[] = "> ";//user stackに置くため配列を用いることでローカル変数に置かれる
    char line[LINE_BUFFER_SIZE];

    while(1){// ずっとinitでいるための無限ループ
        user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(prompt), 2);
        long length = 0;
        char c      = 0;

        while (user_syscall(SyscallNo::kRead, 0, reinterpret_cast<long>(&c), 1) == 1)
        {
            user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(&c), 1);
            line[length++] = c;
            if (c == '\n' || length == sizeof(line))
                break;
        }
        user_syscall(SyscallNo::kWrite, 1, reinterpret_cast<long>(line), length);
    }// init
}

