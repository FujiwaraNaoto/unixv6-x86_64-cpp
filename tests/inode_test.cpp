#include <cstdint>
#include <cstring>
#include "tests.hpp"
#include "inode.hpp"
#include "file_system.hpp"

namespace tests
{
namespace
{

// 書き込みの検証用パターン。offset ごとに違う値になるようにする。
uint8_t pattern(uint32_t offset)
{
    return static_cast<uint8_t>((offset * 7 + 13) & 0xFF);
}

void report(IConsole *console, const char *name, bool ok)
{
    console->set_color(ok ? Color::LightGreen : Color::LightRed, Color::Black);
    console->puts("[INODE] ");
    console->set_color(Color::LightGrey, Color::Black);
    console->printf("%s: %s\n", name, ok ? "OK" : "NG");
}

} // namespace

// inode: 確保 / 参照 / ブロック対応 / 読み書きを確認する。
void inode_read_write(IConsole *console)
{
    using namespace FileSystem;

    // ─── 段階1: allocate_inode / get_inode / update_inode ───
    const auto inum = allocate_inode(InodeType::kFile);
    if (!inum)
    {
        report(console, "allocate_inode", false);
        return;
    }
    Inode *ip = get_inode(*inum);
    if (ip == nullptr)
    {
        report(console, "get_inode", false);
        return;
    }
    report(console, "allocate_inode / get_inode",
           ip->disk.type == InodeType::kFile && ip->disk.size == 0 && ip->ref == 1);

    // 同じ inum を取ると同じ実体が返り、参照数だけが増える
    Inode *same = get_inode(*inum);
    report(console, "get_inode shares entry", same == ip && ip->ref == 2);
    put_inode(same);

    // ─── 段階2: block_map ───
    // 12 番目 (0 始まり) は間接ブロックに入る最初のブロック
    const auto indirect_first = block_map(ip, NDIRECT, true);
    const auto indirect_again = block_map(ip, NDIRECT, false);
    report(console, "block_map indirect",
           indirect_first && indirect_again && *indirect_first == *indirect_again);

    const auto direct_first = block_map(ip, 0, true);
    report(console, "block_map direct", direct_first.has_value());

    // 最大サイズを超える位置は割り当てられない
    report(console, "block_map over MAXFILE", !block_map(ip, MAXFILE, true));

    // ─── 段階3: write_inode / read_inode ───
    // ブロック境界 (512) をまたぐ長さで書く
    constexpr uint32_t LENGTH = 1000;
    static uint8_t source[LENGTH];
    static uint8_t loaded[LENGTH];
    for (uint32_t i = 0; i < LENGTH; i++)
    {
        source[i] = pattern(i);
    }

    truncate_inode(ip); // block_map で確保した分を戻し、size を 0 にしてから書く

    const auto written = write_inode(ip, source, 0, LENGTH);
    std::memset(loaded, 0, LENGTH);
    const auto read = read_inode(ip, loaded, 0, LENGTH);

    bool same_content = written && read && *written == LENGTH && *read == LENGTH;
    for (uint32_t i = 0; same_content && i < LENGTH; i++)
    {
        same_content = loaded[i] == source[i];
    }
    report(console, "write_inode / read_inode", same_content && ip->disk.size == LENGTH);

    // 末尾を超える読み出しは、読めた分だけ返す
    const auto tail = read_inode(ip, loaded, LENGTH - 10, 100);
    report(console, "read_inode clamps at EOF", tail && *tail == 10);

    // 追記して size が伸びるか
    const auto appended = write_inode(ip, source, LENGTH, 24);
    report(console, "write_inode appends", appended && *appended == 24 && ip->disk.size == LENGTH + 24);

    // 間接ブロックを使う位置 (12 ブロック目以降) への書き込み
    const uint32_t indirect_offset = NDIRECT * FSBLOCK_SIZE;
    static uint8_t marker[16];
    static uint8_t marker_read[16];
    for (uint32_t i = 0; i < sizeof(marker); i++)
    {
        marker[i] = pattern(indirect_offset + i);
    }
    truncate_inode(ip);
    const auto zero_filled = write_inode(ip, source, 0, indirect_offset); // 12 ブロック分埋める
    const auto in_indirect = write_inode(ip, marker, indirect_offset, sizeof(marker));
    const auto back        = read_inode(ip, marker_read, indirect_offset, sizeof(marker));
    report(console, "write/read via indirect block",
           zero_filled && in_indirect && back && *back == sizeof(marker) &&
               std::memcmp(marker, marker_read, sizeof(marker)) == 0);

    // ─── 後始末: truncate_inode と put_inode ───
    truncate_inode(ip);
    report(console, "truncate_inode", ip->disk.size == 0 && ip->disk.addrs[0] == 0 && ip->disk.addrs[NDIRECT] == 0);

    ip->disk.nlink = 0; // リンクが無い = put_inode で未使用に戻るはず
    update_inode(ip);
    put_inode(ip);
    Inode *after = get_inode(*inum);
    report(console, "put_inode frees inode", after != nullptr && after->disk.type == InodeType::kUnused);
    put_inode(after);
}

} // namespace tests
