#include <stdlib.h>
#include <stdio.h>
#include <pth.h>
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "merecal.h"

int main(int nb_args, char *args[])
{
	if_fail(init("merecal.log", nb_args, args)) return EXIT_FAILURE;
	char const *folders[] = { "/calendars/rixed", "/calendars/project X" };
	GtkWidget *cal_window = make_cal_window(sizeof_array(folders), folders);
	if (! cal_window) return EXIT_FAILURE;
	gtk_widget_show_all(cal_window);
	gtk_main();
	return EXIT_SUCCESS;
}
