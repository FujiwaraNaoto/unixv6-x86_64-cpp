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
    kstring() { clear(); }
    kstring(const char *s) { assign(s); }

    kstring &operator=(const char *s)
    {
        assign(s);
        return *this;
    }

    const char *c_str() const { return data_; }
    size_t size() const { return len_; }
    bool empty() const { return len_ == 0; }
    static constexpr size_t capacity() { return N - 1; }

    char operator[](size_t i) const { return data_[i]; }

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
    bool operator!=(const char *s) const { return !(*this == s); }

  private:
    void clear()
    {
        len_     = 0;
        data_[0] = '\0';
    }

    void assign(const char *s)
    {
        len_ = 0;
        if (s)
        {
            while (s[len_] != '\0' && len_ < N - 1)
            {
                data_[len_] = s[len_];
                ++len_;
            }
        }
        data_[len_] = '\0';
    }

    char data_[N];
    size_t len_;
};
