#include <stdlib.h>
#include <stdio.h>
#include <pth.h>
#include "meremail.h"

int main(int nb_args, char *args[])
{
	if_fail(init("meremail.log", nb_args, args)) return EXIT_FAILURE;
	GtkWidget *folder_window = make_folder_window("/");
	if (! folder_window) return EXIT_FAILURE;
	gtk_widget_show_all(folder_window);
	gtk_main();
	return EXIT_SUCCESS;
}
