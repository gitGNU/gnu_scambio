#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <log.h>
#include "conf.h"

static void my_print(char const *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

int main(void)
{
	(void)log_init();
	struct conf *conf = conf_new("conf1.sample");
	assert(conf);
	conf_dump(conf, my_print);
	conf_del(conf);
	return EXIT_SUCCESS;
}
