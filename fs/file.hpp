#ifndef FILE_HPP
#define FILE_HPP

#include "console.hpp"
#include "inode.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

// fd (= file descriptor 整数) と inode の間の層。
//   Process::files[fd] → FileDescriptor → (StandardInput / StandardOutput / FileIO)
//
// inode と分けているのは、読み書き位置 (offset) を inode ではなく FileIO が持つため。
// 同じファイルを 2 回 open すれば offset は別々になり、fork した親子は同じ FileDescriptor を
// 指すので offset を共有する。
//
// read / write は処理したバイト数を返し、失敗は static_cast<size_t>(-1) を返す。
// その種類のファイルでできない操作 (標準入力への write など) も -1 を返す (Unix の EBADF に相当)。

class FileDescriptor
{
    public:
        static constexpr size_t kError = static_cast<size_t>(-1);

        virtual ~FileDescriptor() = default;
        FileDescriptor()=default;
        virtual size_t read(void* buffer,size_t len) = 0;
        virtual size_t write(const void* buffer,size_t len) = 0;
        size_t close(){
            if(refcnt<=0) return 0;
            refcnt--;
            if(refcnt==0) release();
            return 0;
        }
        bool in_use() const {return refcnt>0;}
        size_t refcount() const {return refcnt;}
        FileDescriptor* duplicate(){
            refcnt++;
            return this;
        }
    protected:
        void start_use(){refcnt=1;} // 派生クラスの open() から呼ぶ。プールの空きスロットを使用中にする
        virtual void release() = 0; // 最後の close で呼ばれる
    private:
        size_t refcnt=0; // fd 表から何か所で指されているか。FileIO では 0 = プールの空きスロット
};

// fd 0 用: キーボードから読むだけ。NOTE: read は 1 行入力されるまで返らない。
// 実体はシステムに 1 つで、全プロセスの fd 0 が参照カウントで共有する。
class StandardInput:  public FileDescriptor
{
    public:
        StandardInput() = default;
        size_t read(void* buffer,size_t len) override;
        size_t write(const void* buffer,size_t len) override;
    private:
        void release() override;
};

// fd 1/2 用: file_set_console で登録したコンソールに書くだけ。
// 実体はシステムに 1 つで、全プロセスの fd 1/2 が参照カウントで共有する。
class StandardOutput:  public FileDescriptor
{
    public:
        StandardOutput() = default;
        size_t read(void* buffer,size_t len) override;
        size_t write(const void* buffer,size_t len) override;
    private:
        void release() override;
};

// ディスク上のファイル。読み書きできるかは open のモードで決まる
class FileIO:  public FileDescriptor
{
    public:
        FileIO() = default;
        void open(InodeRef inoderef_,bool readable_,bool writable_){
            start_use();
            inoderef = std::move(inoderef_);
            offset   = 0;
            readable = readable_;
            writable = writable_;
        }
        size_t read(void* buffer,size_t len) override;
        size_t write(const void* buffer,size_t len) override;
    private:
        void release() override;
        InodeRef inoderef;
        uint32_t offset = 0;
        bool readable=false;
        bool writable = false;
};

namespace FileSystem
{

constexpr int NUM_FILES = 100; // システム全体で開ける FileIO の数 (xv6 の NFILE)

// StandardOutput の書き込み先を登録する。nullptr なら null_console に捨てる。
void file_set_console(IConsole *console);

// 唯一の StandardInput / StandardOutput の参照カウントを増やして返す。
FileDescriptor *open_standard_input();
FileDescriptor *open_standard_output();
// プールの空きスロットを refcnt = 1 で返す。満杯なら nullptr。
FileDescriptor *open_file(InodeRef inode, bool readable, bool writable);

} // namespace FileSystem

#endif // FILE_HPP
