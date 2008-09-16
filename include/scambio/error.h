#ifndef ERROR_H_080911
#define ERROR_H_080911

#include <stdbool.h>
#include <errno.h>

/* When an error is encountered, a function may want to clean its environment
 * before returning to the caller. While doing so other errors may be
 * encountered. All these must be cleaned so that they do not interfere
 * with the meaningfull error stack. This can be done with aknowledging
 * the errors so far (which reset is_error() to false), and returning to that
 * level thereafter.
 * In other words : you are not allowed to _call_ a function when is_error()
 * is true (at least not a function that use this error system).
 */

bool is_error(void);
int error_code(void);
char const *error_str(void);
void error_push(int code, char *fmt, ...);

void error_ack();	// set the expected depth of the error stack to be as it is now
void error_clear();	// pop all errors up to the acknoledged level, then pop this level

#define on_error if (is_error())
#define unless_error if (!is_error())
#define with_error(code, ...) if (error_push((code), __VA_ARGS__), 1)

#endif
