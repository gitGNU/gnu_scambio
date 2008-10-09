#ifndef MERELIB_H_081008
#define MERELIB_H_081008

#include <gtk/gtk.h>

void destroy_cb(GtkWidget *widget, gpointer data);
void alert(GtkMessageType type, char const *text);
GtkWidget *make_window(void (*cb)(GtkWidget *, gpointer));
GtkWidget *make_labeled_hbox(char const *label, GtkWidget *other);
void init(char const *logfile, int nb_args, char *args[]);

#endif
