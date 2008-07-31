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
