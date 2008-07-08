#ifndef MDIR_H_080708
#define MDIR_H_080708

/* Interface to a mdird client mdir */

struct mdir;
int mdir_get(struct mdir **, char const *path);
void mdir_unref(struct mdir *);

#endif
