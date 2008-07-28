#ifndef MISC_H_080625
#define MISC_H_080625

#include <stddef.h>	// size_t (? FIXME)
#include <stdbool.h>
int Write(int fd, void const *buf, size_t len);
int Read(void *buf, int fd, off_t offset, size_t len);
int Mkdir(char const *path);
// a line is said to match a delim if it starts with the delim, and is followed only by optional spaces
bool line_match(char *restrict line, char *restrict delim);

void path_push(char path[], char const *next);
void path_pop(char path[]);

#endif
