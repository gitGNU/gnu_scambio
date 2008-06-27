#include <assert.h>
#include <errno.h>
#include <pth.h>
#include "scambio.h"
#include "misc.h"

int Write(int fd, void const *buf, size_t len)
{
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

