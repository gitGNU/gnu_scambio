/* Persistant data, ie a struct that's also present on a disk file.
 * Aka very simple wrapper to mmap.
 */
#ifndef PERSIST_H_080628
#define PERSIST_H_080628

struct persist {
	size_t size;
	void *data;
};

int persist_ctor(struct persist *, size_t size, char const *fname);
void persist_dtor(struct persist *);

#endif
