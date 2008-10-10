#ifndef MERELIB_H_081008
#define MERELIB_H_081008

#include <stdbool.h>
#include <gtk/gtk.h>

void init(char const *logfile, int nb_args, char *args[]);
void destroy_cb(GtkWidget *widget, gpointer data);
void alert(GtkMessageType type, char const *text);
bool confirm(char const *);
GtkWidget *make_window(void (*cb)(GtkWidget *, gpointer));
GtkWidget *make_labeled_hbox(char const *label, GtkWidget *other);
GtkWidget *make_labeled_hboxes(unsigned nb_rows, ...);
GtkWidget *make_toolbar(unsigned nb_buttons, ...);

#endif
