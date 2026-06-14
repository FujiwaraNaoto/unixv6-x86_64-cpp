#pragma once


class IConsole
{
  public:
    virtual void putchar(char c)     = 0;
    virtual void puts(const char *s) = 0;
    virtual ~IConsole()              = default;
};
