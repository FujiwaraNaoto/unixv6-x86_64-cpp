#ifndef COMMAND_HPP
#define COMMAND_HPP

#include <cstddef>

void put(const char *s, size_t n);
void put_char(char c);
long read_line(char *line, size_t capacity);
int split(char *line, char **argv, int max_args);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);

#endif // COMMAND_H
