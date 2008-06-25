#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include "scambio.h"
#include "conf.h"

/* NOTICE: this should not log anything since log depends on conf */

/*
 * Data Definition
 */

/*
 * Configuration queries
 */

int conf_set_default_str(char const *name, char const *value)
{
	if (0 != setenv(name, value, 0)) return -errno;
	return 0;
}

int conf_set_default_int(char const *name, long long value)
{
	int len = snprintf(NULL, 0, "%lld", value);
	char *str = malloc(len+1);	// is the string _copied_ into the environment ?
	if (! str) return -ENOMEM;
	snprintf(str, len+1, "%lld", value);
	int ret = conf_set_default_str(name, str);
	free(str);
	return ret;
}

char const *conf_get_str(char const *name)
{
	return getenv(name);
}

long long conf_get_int(char const *name)
{
	char const *str = conf_get_str(name);
	assert(str);
	if (! str) return -ENOENT;
	char *end;
	long long value = strtoll(str, &end, 0);
	assert(*end == '\0');
	return value;
}

