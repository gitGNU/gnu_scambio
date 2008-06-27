#include <stdlib.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <log.h>
#include "stribution.h"
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
	struct stribution *strib = strib_new("conf1.sample");
	assert(strib);
	strib_dump(strib, my_print);
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
	struct strib_action const *actions[strib->nb_tests];
	unsigned nb_actions = strib_eval(strib, head, actions);
	assert(nb_actions == 3);
	header_del(head);
	free(head_mutable);
	strib_del(strib);
	return EXIT_SUCCESS;
}
