#pragma once
#include <cstddef>

// 固定容量の value 型文字列。
//
// ヒープ・例外・libstdc++ に依存しないフリースタンディング向けの簡易文字列。
// 容量 N バイト (= 最大 N-1 文字 + null 終端) を内包し、超過分は切り捨てる。
// std::string ライクに代入・比較・c_str()/size() が使える。
template <size_t N>
class kstring
{
    static_assert(N > 0, "kstring capacity must be > 0");

  public:
    kstring()
    {
        clear();
    }
    kstring(const char *s)
    {
        assign(s);
    }
    // 最大 n バイトまで読み込む。途中に NUL があればそこで止める。
    // NUL 終端されているとは限らない固定長フィールドから作るときに使う
    // (copy_to() の逆操作)。
    kstring(const char *s, size_t n)
    {
        assign(s, n);
    }

    kstring &operator=(const char *s)
    {
        assign(s);
        return *this;
    }

    // コピーはメンバ単位で行う。
    // 既定のコピー代入は構造体のパディングごと転送しうるため、コピー元の
    // 未初期化パディングがコピー先に持ち込まれる。この型をディスク上の
    // 構造体に埋め込んでいる箇所があるので、パディングには触れない。
    kstring(const kstring &other) : len_(other.len_)
    {
        for (size_t i = 0; i < N; ++i)
        {
            data_[i] = other.data_[i];
        }
    }
    kstring &operator=(const kstring &other)
    {
        if (this != &other)
        {
            len_ = other.len_;
            for (size_t i = 0; i < N; ++i)
            {
                data_[i] = other.data_[i];
            }
        }
        return *this;
    }

    const char *c_str() const
    {
        return data_;
    }
    size_t size() const
    {
        return len_;
    }
    bool empty() const
    {
        return len_ == 0;
    }
    static constexpr size_t capacity()
    {
        return N - 1;
    }

    char operator[](size_t i) const
    {
        return data_[i];
    }

    // 固定長フィールドへ書き出す。dest の n バイトをちょうど埋め、
    // 文字数が足りない分はゼロ埋め、n を超える分は切り捨てる。
    // NUL 終端は付けない (n バイト使い切る場合があるため)。
    // ディスク上の固定長の名前フィールドなどに書くときに使う。
    void copy_to(char *dest, size_t n) const
    {
        size_t i = 0;
        for (; i < n && i < len_; i++)
        {
            dest[i] = data_[i];
        }
        for (; i < n; i++)
        {
            dest[i] = '\0';
        }
    }

    bool operator==(const char *s) const
    {
        if (s == nullptr)
            return false;
        size_t i = 0;
        for (; i < len_; ++i)
        {
            if (s[i] != data_[i])
                return false;
        }
        return s[i] == '\0';
    }
    bool operator!=(const char *s) const
    {
        return !(*this == s);
    }

  private:
    void clear()
    {
        assign(nullptr, 0); // 未使用領域までゼロ埋めする
    }

    void assign(const char *s)
    {
        assign(s, N - 1); // 容量いっぱいまで NUL を探す
    }

    void assign(const char *s, size_t n)
    {
        len_ = 0;
        if (s)
        {
            while (len_ < n && len_ < N - 1 && s[len_] != '\0')
            {
                data_[len_] = s[len_];
                ++len_;
            }
        }
        // 終端だけでなく末尾の未使用領域まですべてゼロで埋める。
        // 埋めないと未初期化のスタック内容が data_ に残り、この型を含む
        // 構造体をディスクやネットワークへそのまま書き出したときに漏れる。
        // 内容が同じなら常に同じバイト列になるので、ダンプの比較もしやすい。
        for (size_t i = len_; i < N; ++i)
        {
            data_[i] = '\0';
        }
    }

    char data_[N];
    size_t len_;
};
