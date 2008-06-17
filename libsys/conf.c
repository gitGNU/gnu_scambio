#include <stdlib.h>
#include <stdio.h>
#include <log.h>
#include <string.h>
#include <errno.h>
#include "conf.h"

/*
 * Data Definition
 */

struct exception conf_no_such_param = { "NoSuchConfParam" };
struct exception conf_bad_integer_format = { "BadIntegerFormat" };


void conf_set_default_str(char const *name, char const *value)
{
	if (0 != setenv(name, value, 0)) THROW(ERRNO);
}

void conf_set_default_int(char const *name, long long value)
{
	unwindb();
	int len = snprintf(NULL, 0, "%lld", value);
	char *str = malloc(len+1);
	atunwind(str, free);	// is the string copied into the environment ?
	if (! str) THROW(OOM);
	snprintf(str, len+1, "%lld", value);
	conf_set_default_str(name, str);
	unwind();
}

char const *conf_get_str(char const *name)
{
	char const *value = getenv(name);
	if (! value) THROW(&conf_no_such_param);
	return value;
}

long long conf_get_int(char const *name)
{
	char const *value = conf_get_str(name);
	char *end;
	long long i = strtoll(value, &end, 0);
	if (*end != '\0') THROW(&conf_bad_integer_format);
	return i;
}

