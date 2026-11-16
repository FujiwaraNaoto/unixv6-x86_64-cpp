#ifndef FILE_HPP
#define FILE_HPP

#include "console.hpp"
#include "inode.hpp"
#include "types.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

// fd (= file descriptor 整数) と inode の間の層。
//   Process::files[fd] (FileRef) → FileDescriptor → (StandardInput / StandardOutput / FileIO)
//
// inode と分けているのは、読み書き位置 (offset) を inode ではなく FileIO が持つため。
// 同じファイルを 2 回 open すれば offset は別々になり、fork した親子は同じ FileDescriptor を
// 指すので offset を共有する。
//
// read / write は処理したバイト数を返し、失敗は kError (-1) を返す。
// その種類のファイルでできない操作 (標準入力への write など) も -1 を返す (Unix の EBADF に相当)。

class FileDescriptor
{
  public:
    virtual ~FileDescriptor()                             = default;
    virtual ssize_t read(void *buffer, size_t len)        = 0;
    virtual ssize_t write(const void *buffer, size_t len) = 0;

  protected:
    FileDescriptor()                                  = default;
    FileDescriptor(const FileDescriptor &)            = delete;
    FileDescriptor &operator=(const FileDescriptor &) = delete;
};

// fd 0 用: キーボードから読むだけ。NOTE: read は 1 行入力されるまで返らない。
// 実体はシステムに 1 つで、全プロセスの fd 0 が参照カウントで共有する。
class StandardInput : public FileDescriptor
{
  public:
    StandardInput() = default;
    ssize_t read(void *buffer, size_t len) override;
    ssize_t write(const void *buffer, size_t len) override;
};

// fd 1/2 用: file_set_console で登録したコンソールに書くだけ。
// 実体はシステムに 1 つで、全プロセスの fd 1/2 が参照カウントで共有する。
class StandardOutput : public FileDescriptor
{
  public:
    StandardOutput() = default;
    ssize_t read(void *buffer, size_t len) override;
    ssize_t write(const void *buffer, size_t len) override;
};

// ディスク上のファイル。読み書きできるかは open のモードで決まる
class FileIO : public FileDescriptor
{
  public:
    FileIO(InodeRef inoderef_, bool readable_, bool writable_)
        : inoderef(std::move(inoderef_)), readable(readable_), writable(writable_)
    {
    }
    ssize_t read(void *buffer, size_t len) override;
    ssize_t write(const void *buffer, size_t len) override;

  private:
    InodeRef inoderef; // FileIO が破棄されると (= 最後の close) inode の参照も手放す
    uint32_t offset = 0;
    bool readable   = false;
    bool writable   = false;
};

namespace FileSystem
{

constexpr int NUM_FILES = 100; // システム全体で開ける FileDescriptor の数 (xv6 の NFILE)

namespace detail
{
// FileDescriptor の実体 1 つ分の置き場所。new が無いので静的な表に並べ、refcnt == 0 を空きとする。
// object は storage に placement new で構築した実体を指す (空きなら nullptr)。
struct FileSlot
{
    size_t refcnt          = 0; // 何個の FileRef から指されているか
    FileDescriptor *object = nullptr;
    alignas(FileIO) std::byte storage[sizeof(FileIO)];
};

// 空きスロットを返す。満杯なら nullptr。使用中にするのは呼び出し側 (make_file)。
FileSlot *find_free_slot();
} // namespace detail

} // namespace FileSystem

// FileDescriptor への参照カウント付きポインタ。fd 表の 1 マスが 1 つの FileRef。
//   コピー = dup (同じ実体を指し offset も共有する)、デストラクタと reset() = close。
// 最後の FileRef が消えると実体を破棄してスロットを空きに戻す。
class FileRef
{
  public:
    FileRef() = default;
    explicit FileRef(FileSystem::detail::FileSlot *slot) : slot_(slot) { } // make_file 専用

    FileRef(const FileRef &other) : slot_(other.slot_)
    {
        if (slot_ != nullptr)
            slot_->refcnt++;
    }
    FileRef(FileRef &&other) noexcept : slot_(std::exchange(other.slot_, nullptr)) { }
    FileRef &operator=(FileRef other) noexcept // copy-and-swap
    {
        std::swap(slot_, other.slot_);
        return *this;
    }
    ~FileRef() { reset(); }

    void reset();

    FileDescriptor *get() const { return slot_ != nullptr ? slot_->object : nullptr; }
    FileDescriptor *operator->() const { return slot_->object; }
    explicit operator bool() const { return slot_ != nullptr; }
    size_t use_count() const { return slot_ != nullptr ? slot_->refcnt : 0; }

  private:
    FileSystem::detail::FileSlot *slot_ = nullptr;
};

namespace FileSystem
{

// 空きスロットに T を構築し、refcnt = 1 の FileRef を返す。満杯なら空の FileRef。
template <class T, class... Args>
FileRef make_file(Args &&...args)
{
    static_assert(std::is_base_of_v<FileDescriptor, T>);
    static_assert(sizeof(T) <= sizeof(detail::FileSlot::storage) && alignof(T) <= alignof(FileIO),
                  "FileSlot::storage に収まらない");
    detail::FileSlot *slot = detail::find_free_slot();
    if (slot == nullptr)
        return FileRef{};
    slot->object = ::new (slot->storage) T(std::forward<Args>(args)...);
    slot->refcnt = 1;
    return FileRef(slot);
}

// StandardOutput の書き込み先を登録する。nullptr なら null_console に捨てる。
void file_set_console(IConsole *console);

// 唯一の StandardInput / StandardOutput への参照を返す (初回だけスロットを 1 つ使う)。満杯なら空。
FileRef open_standard_input();
FileRef open_standard_output();
// inode を FileIO で開く。inode が空かスロットが満杯なら空の FileRef。
FileRef open_file(InodeRef inode, bool readable, bool writable);

} // namespace FileSystem

#endif // FILE_HPP
