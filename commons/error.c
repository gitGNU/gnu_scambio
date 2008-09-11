#include <stdarg.h>
#include <strio.h>
#include <strings.h>
#include "scambio.h"
#include "error.h"

static struct error {
	int code;
	char str[1024];
} err_stack[64];
static unsigned nb_errors = 0;
static unsigned nb_acks = 1;
static unsigned ack_stack[16] = { 0 };

static unsigned nb_expected_errors(void)
{
	return ack_stack[nb_acks-1];
}

bool is_error(void)
{
	return nb_errors > nb_expected_errors();
}

int error_code(void)
{
	assert(nb_errors);
	return err_stack[nb_errors-1].code;
}

char const *error_str(void)
{
	assert(nb_errors);
	return err_stack[nb_errors-1].str;
}

void error_push(int code, char *fmt, ...)
{
	assert(nb_errors < sizeof_array(err_stack));
	struct error *const err = err_stack + nb_errors++;
	err->code = code;
	int len = 0;
	if (fmt) {
		va_list ap;
		va_start(ap, fmt);
		len = vsnprintf(err->str, sizeof(err->str), fmt, ap);
		if (len >= sizeof(err->str)) len = sizeof(err->str)-1;
		va_end(ap);
	}
	snprintf(err->str+len, "%s%s", len > 0 ? " : ":"", strerror(code));
	error(err->str);
}

void error_pop()
{
	assert(nb_errors >= nb_expected_errors());
	nb_errors = nb_expected_errors();
	assert(nb_acks > 0);
	nb_acks--;
}

