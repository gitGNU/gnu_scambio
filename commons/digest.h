#ifndef DIGEST_H_080725
#define DIGEST_H_080725

#define MAX_DIGEST_LEN 128
#include <stddef.h> // size_t

/* Compute a message digest based on the header. Make it long enought to be
 * discriminent, but not too much that it still fit on this fixed sized array.
 * Also, you are not allowed to return with an error. Go ahead, Dilbert !
 * Returns the actual length of the string.
 */
size_t digest(char *out, size_t len, char const *in);

#endif