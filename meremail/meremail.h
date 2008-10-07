#ifndef MEREMAIL_H_081007
#define MEREMAIL_H_081007

#include <gtk/gtk.h>

void destroy_cb(GtkWidget *widget, gpointer data);
void alert(char const *text);
GtkWidget *make_window(void (*cb)(GtkWidget *, gpointer));

// List view

GtkWidget *make_list_window(char const *folder);

// Folders view

GtkWidget *make_folder_window(char const *parent);

#endif
