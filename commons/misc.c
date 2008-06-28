#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pth.h>
#include "scambio.h"
#include "misc.h"

int Write(int fd, void const *buf, size_t len)
{
	debug("Write(%d, %p, %zu)", fd, buf, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_write(fd, buf + done, len - done);
		if (ret < 0) {
			if (errno != EINTR) return -errno;
			continue;
		}
		done += ret;
	}
	assert(done == len);
	return 0;
}

int Read(void *buf, int fd, off_t offset, size_t len)
{
	debug("Read(%p, %d, %zu)", buf, fd, len);
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_pread(fd, buf + done, len - done, offset + done);
		if (ret < 0) {
			if (errno != EINTR) return -errno;
			continue;
		}
		done += ret;
	}
	assert(done == len);
	return 0;
}

static int Mkdir_single(char const *path)
{
	if (0 != mkdir(path, 0744) && errno != EEXIST) {
		error("mkdir '%s' : %s", path, strerror(errno));
		return -errno;
	}
	return 0;
}

int Mkdir(char const *path_)
{
	debug("Mkdir(%s)", path_);
	char path[PATH_MAX];
	snprintf(path, sizeof(path), "%s", path_);
	char *c = path;
	if (! *c) return -EINVAL;
	for (c = path + 1; *c != '\0'; c++) {
		if (*c == '/') {
			*c = '\0';
			int err = Mkdir_single(path);
			if (err) return err;
			*c = '/';
		}
	}
	return Mkdir_single(path);
}

