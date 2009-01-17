/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include "digest.h"
#include "scambio.h"

static char tohex(unsigned i)
{
	if (i < 10) return i+'0';
	return i-10+'A';
}

static size_t stringify(char *str_, size_t digest_bytes, unsigned char const *digest)
{
	char *str = str_;
	while (digest_bytes--) {
		unsigned char byte = *digest++;
		*str++ = tohex(byte&0xf);
		*str++ = tohex((byte>>4)&0xf);
	}
	*str = '\0';
	return str - str_;
}

#if HAVE_LIBGNUTLS

#include <gnutls/gnutls.h>
size_t digest(char *out, size_t len, char const *in)
{
	gnutls_datum_t datum = {
		.data = (unsigned char *)in,	// Oh mighty GNU please don't write in there
		.size = len,
	};
	unsigned char result[MAX_DIGEST_LEN/2 +1];
	size_t size = sizeof(result);
	if (0 > gnutls_fingerprint(GNUTLS_DIG_SHA1, &datum, result, &size)) {
		fatal("The digest function that can't fail failed. Think how few people can see this message !");
	}
	return stringify(out, size, result);
}

#elif HAVE_LIBSSL

#include <openssl/sha.h>
size_t digest(char *out, size_t len, char const *in)
{
	/* FIXME: 
	 * "The EVP interface to message digests should almost always be used in preference to the low level interfaces. This is
	 *  because the code then becomes transparent to the digest used and much more flexible."
	 */
	assert(MAX_DIGEST_LEN >= SHA_DIGEST_LENGTH*2);	// we use standard 4 bytes per character representation
	unsigned char compact_digest[SHA_DIGEST_LENGTH];
	(void)SHA1((unsigned char const *)in, len, compact_digest);
	return stringify(out, sizeof(compact_digest), compact_digest);
}

#else

#	error no digest function available. Use gnutls or openssl.

#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "misc.h"

size_t digest_file(char *out, char const *filename)
{
	debug("filename='%s'", filename);
	size_t ret = 0;
	// Stupid method : read the whole file in RAM, then compute its digest
	int fd = open(filename, O_RDONLY);
	if (fd < 0) with_error(errno, "open(%s)", filename) return 0;
	do {
		off_t size = filesize(fd);
		on_error break;
		char *in = malloc(size);
		if (! in) with_error(ENOMEM, "malloc file %s", filename) break;
		Read(in, fd, size);
		unless_error ret = digest(out, size, in);
		free(in);
	} while (0);
	(void)close(fd);
	return ret;
}

