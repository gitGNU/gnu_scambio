#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "scambio.h"
#include "persist.h"

static int open_or_create(char const *fname, size_t size)
{
	int fd = open(fname, O_RDWR);
	if (fd >= 0) return fd;	// FIXME: check size nonetheless
	if (errno != ENOENT) return -errno;
	fd = open(fname, O_RDWR|O_CREAT|O_EXCL, 0660);
	if (fd < 0) return -errno;
	if (0 != ftruncate(fd, size)) {
		(void)close(fd);
		return -errno;
	}
	return fd;
}

int persist_ctor(struct persist *p, size_t size, char const *fname)
{
	p->size = size;
	int fd = open_or_create(fname, size);
	if (fd < 0) return fd;
	p->data = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	(void)close(fd);
	if (MAP_FAILED == p->data) return -errno;
	return 0;
}

void persist_dtor(struct persist *p)
{
	(void)munmap(p->data, p->size);
}

