
BITS 64
section .text
global fork_child_return
extern fork_child_setup
extern syscall_return_path
fork_child_return:
    ; 子のカーネルスタックは親のコピー。
    ; syscallフレームが積まれた状態なので、RAX=0 にして
    ; syscall_entry の復帰処理と同じ手順でユーザーへ戻る。
    xor rax,rax ; fork() の戻り値は 0
    ; 以降は syscall_entry のレジスタ復元 + sysret と同じ
    ; (実装は fork_child_setup で調整するか、
    ;  syscall_entry の復帰部分を共有する)
    jmp syscall_return_path ; syscall_return へジャンプ
