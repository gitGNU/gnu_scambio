#include <assert.h>
#include <errno.h>
#include <pth.h>
#include "scambio.h"
#include "misc.h"

int Write(int fd, void const *buf, size_t len)
{
	size_t done = 0;
	while (done < len) {
		ssize_t ret = pth_write(fd, buf, len);
		if (ret < 0) {
			if (errno != EINTR) return -errno;
			continue;
		}
		done += ret;
	}
	assert(done == len);
	return 0;
}

