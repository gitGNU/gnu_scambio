#ifndef MERELIB_H_081008
#define MERELIB_H_081008

#include <gtk/gtk.h>

void destroy_cb(GtkWidget *widget, gpointer data);
void alert(GtkMessageType type, char const *text);
GtkWidget *make_window(void (*cb)(GtkWidget *, gpointer));
void init(char const *logfile, int nb_args, char *args[]);

#endif
