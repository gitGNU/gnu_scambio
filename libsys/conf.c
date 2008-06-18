#include <stdlib.h>
#include <stdio.h>
#include <log.h>
#include <string.h>
#include <errno.h>
#include <object.h>
#include "conf.h"

/*
 * Data Definition
 */

/*
 * Exceptions for requesting an unknown parameter
 */

struct enoparam {
	struct exception exception;
	char *name;
};

static void enoparam_del(struct exception *e)
{
	struct enoparam *enp = DOWNCAST(e, exception, enoparam);
	free(enp->name);
	free(enp);
}

static char const *enoparam_name(struct exception *e, size_t size, char *buf)
{
	struct enoparam *enp = DOWNCAST(e, exception, enoparam);
	snprintf(buf, size, "NoSuchParam : %s", enp->name);
	return buf;
}

struct exception_ops const conf_no_such_param_ops = { .del = enoparam_del, .name = enoparam_name };

struct exception *conf_no_such_param(char const *param)
{
	struct enoparam *enp = malloc(sizeof(*enp));
	assert(enp);
	enp->exception.ops = &conf_no_such_param_ops;
	enp->name = strdup(param);
	return &enp->exception;
}

/*
 * Exceptions for requesting an integer value from a non-int string
 */

struct ebadint {
	struct exception exception;
	char *name, *format;
};

static void ebadint_del(struct exception *e)
{
	struct ebadint *ebi = DOWNCAST(e, exception, ebadint);
	free(ebi->name);
	free(ebi->format);
}

static char const *ebadint_name(struct exception *e, size_t size, char *buf)
{
	struct ebadint *ebi = DOWNCAST(e, exception, ebadint);
	snprintf(buf, size, "BadIntegerFormat : %s for parameter %s", ebi->format, ebi->name);
	return buf;
}

struct exception_ops const conf_bad_integer_format_ops = { .del = ebadint_del, .name = ebadint_name };

struct exception *conf_bad_integer_format(char const *param, char const *format)
{
	struct ebadint *ebi = malloc(sizeof(*ebi));
	assert(ebi);
	ebi->exception.ops = &conf_bad_integer_format_ops;
	ebi->name = strdup(param);
	ebi->format = strdup(format);
	return &ebi->exception;
}

/*
 * Configuration queries
 */

void conf_set_default_str(char const *name, char const *value)
{
	if (0 != setenv(name, value, 0)) THROW(exception_sys(errno));
}

void conf_set_default_int(char const *name, long long value)
{
	int len = snprintf(NULL, 0, "%lld", value);
	char *str = malloc_or_throw_uwprotect(len+1);	// is the string _copied_ into the environment ?
	snprintf(str, len+1, "%lld", value);
	conf_set_default_str(name, str);
	unwind;
}

char const *conf_get_str(char const *name)
{
	char const *value = getenv(name);
	if (! value) THROW(conf_no_such_param(name));
	return value;
}

long long conf_get_int(char const *name)
{
	char const *value = conf_get_str(name);
	char *end;
	long long i = strtoll(value, &end, 0);
	if (*end != '\0') THROW(conf_bad_integer_format(name, value));
	return i;
}

