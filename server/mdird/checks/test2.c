#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <log.h>
#include "conf.h"
#include "header.h"

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
	// Now add a header
	char *head_mutable = strdup(
		"From: grosminet@acme.org\n"
		"Truc: Muche\n"
		"Machin: Chose\n"
		"Content-Length: 7\n"
		"Subject: [TODO] manger titi\n"
	);
	struct header *head = header_new(head_mutable);
	assert(head);
	struct conf_action const *actions[conf->nb_tests];
	unsigned nb_actions = conf_eval(conf, head, actions);
	assert(nb_actions == 3);
	header_del(head);
	free(head_mutable);
	conf_del(conf);
	return EXIT_SUCCESS;
}
