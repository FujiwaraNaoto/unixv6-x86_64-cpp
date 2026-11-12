#ifndef FILE_HPP
#define FILE_HPP

#include "console.hpp"
#include "inode.hpp"

#include <cstdint>
#include <optional>

// fd (= file descriptor 整数) と inode の間の層。
//   Process::files[fd] → File → InodeRef
//
// inode と分けているのは、読み書き位置 (offset) を inode ではなく File が持つため。
// 同じファイルを 2 回 open すれば offset は別々になり、fork した親子は同じ File を
// 指すので offset を共有する。コンソールも同じ口で読み書きできる。

enum class FileType : uint8_t
{
    kNone,    // 空きスロット
    kConsole, // fd 0/1/2 用
    kInode,   // ディスク上のファイル
};

class FileDescriptor
{
    public:
        virtual ~FileDescriptor() = default;
        FileDescriptor()=default;
        FileDescriptor(bool readable_,bool writable_):readable(readable_),writable(writable_),refcnt(1){};
        virtual size_t read(void* buffer,size_t len) = 0;
        virtual size_t write(void* buffer,size_t len) = 0;
        size_t close(){
            if(refcnt<=0) return 0;
            refcnt--;
            if(refcnt==0) release();
            return 0;
        }
        bool in_use() const {return refcnt>0;}
        FileDescriptor* duplicate(){
            refcnt++;
            return this;
        }
    protected:
        virtual void release() = 0;
        bool readable = false;
        bool writable = false;
    private:
        size_t refcnt=0;
};

class StandardIO:  public FileDescriptor
{
    public:
        StandardIO() = default;
        StandardIO(IConsole* console_,bool readable_=false,bool writable_=false):
            io_console(console_),FileDescriptor(readable_,writable_){};
        size_t read(void* buffer,size_t len) override;
        size_t write(void* buffer,size_t len) override;
    
    private:
        void release() override;
        IConsole* io_console;
};

class FileIO:  public FileDescriptor
{
    public:
        //FileIO(const InodeRef& inoderef_):inoderef(inoderef_){};
        size_t read(void* buffer,size_t len) override;
        size_t write(void* buffer,size_t len) override;
    private:
        void release() override;
        InodeRef inoderef;
        size_t offset = 0;
};


namespace FileSystem{
    StandardIO *open_console(bool readable,bool writable);
}


struct File
{
    FileType type = FileType::kNone;
    int refcnt    = 0; // fd 表から何か所で指されているか
    bool readable = false;
    bool writable = false;
    InodeRef inode;      // kInode のときだけ有効
    uint32_t offset = 0; // kInode のときの読み書き位置
};

namespace FileSystem
{

constexpr int NUM_FILES = 100; // システム全体で開けるファイル数 (xv6 の NFILE)

// kConsole の書き込み先を登録する。nullptr なら null_console に捨てる。
void file_set_console(IConsole *console);

File *file_allocate();            // 空きスロットを refcnt = 1 で返す。満杯なら nullptr
File *file_duplicate(File *file); // refcnt++ して同じ File を返す (fork 用)。nullptr はそのまま返す
void file_close(File *file);      // refcnt--。0 になったら inode を手放して空きに戻す

// kConsole の File を作る。満杯なら nullptr。
File *file_open_console(bool readable, bool writable);

// 現在の offset から読み書きして offset を進める。処理したバイト数を返す。
// 読めない / 書けない File と、下の層の失敗は std::nullopt。ファイル末尾を読むと 0。
// NOTE: kConsole の読み込みは 1 行入力されるまで返らない。
std::optional<uint32_t> file_read(File &file, uint8_t *dest, uint32_t count);
std::optional<uint32_t> file_write(File &file, const uint8_t *source, uint32_t count);

} // namespace FileSystem

#endif // FILE_HPP
