#include <string.h>
#include <time.h>
#include <assert.h>
#include "merecal.h"

/*
 * Data Definitions
 */

/*
 * Callbacks
 */

static void close_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	debug("close");
	GtkWidget *window = (GtkWidget *)user_data;
	gtk_widget_destroy(window);
}

/*
 * Build the view
 */

GtkWidget *make_edit_window(struct cal_folder *default_cf, struct cal_date *start, struct cal_date *stop, char const *descr)
{
	GtkWidget *window = make_window(NULL);

	// First the combo to choose the folder from
	GtkWidget *folder_combo = gtk_combo_box_new_text();
	gtk_combo_box_set_title(GTK_COMBO_BOX(folder_combo), "Folder");
	struct cal_folder *cf;
	int i = 0;
	LIST_FOREACH(cf, &cal_folders, entry) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(folder_combo), cf->name);
		if (cf == default_cf) gtk_combo_box_set_active(GTK_COMBO_BOX(folder_combo), i);
		i++;
	}

	// Then two date editors (text inputs), the second one being optional
	GtkWidget *table = gtk_table_new(2, 2, FALSE);
	GtkWidget *start_label = gtk_label_new("From :");
	gtk_table_attach(GTK_TABLE(table), start_label, 0, 1, 0, 1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 1, 1);
	GtkWidget *start_entry = gtk_entry_new_with_max_length(100);
	gtk_entry_set_text(GTK_ENTRY(start_entry), start->str);
	gtk_table_attach(GTK_TABLE(table), start_entry, 1, 2, 0, 1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 1, 1);
	GtkWidget *stop_label = gtk_label_new("To :");
	gtk_table_attach(GTK_TABLE(table), stop_label, 0, 1, 1, 2, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 1, 1);
	GtkWidget *stop_entry = gtk_entry_new_with_max_length(100);
	gtk_entry_set_text(GTK_ENTRY(stop_entry), stop->str);
	gtk_table_attach(GTK_TABLE(table), stop_entry, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 1, 1);
	
	// Then a text editor for the description
	// FIXME: replace this with a multiline area (later new lines will be replaced by some UTF8 code)
	GtkWidget *descr_entry = gtk_entry_new_with_max_length(1000);
	gtk_entry_set_text(GTK_ENTRY(descr_entry), descr);
	
	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	GtkToolItem *button_cancel = gtk_tool_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_cancel, -1);
	g_signal_connect(G_OBJECT(button_cancel), "clicked", G_CALLBACK(close_cb), window);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(vbox), folder_combo);
	gtk_container_add(GTK_CONTAINER(vbox), table);
	gtk_container_add(GTK_CONTAINER(vbox), descr_entry);
	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_START);

	gtk_container_add(GTK_CONTAINER(window), vbox);
	return window;
}

