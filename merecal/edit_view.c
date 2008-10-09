#include <string.h>
#include <time.h>
#include <assert.h>
#include "merecal.h"

/*
 * Data Definitions
 */

struct editor {
	GtkWidget *window, *folder_combo, *start_entry, *stop_entry;
	GtkTextBuffer *descr_buffer;
	struct cal_event *replaced;
};

/*
 * Callbacks
 */

static void editor_del(struct editor *e)
{
	gtk_widget_destroy(e->window);
	free(e);
}

static void close_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	debug("close");
	editor_del((struct editor *)user_data);
}

static char *get_serial_text(GtkTextBuffer *buffer)
{
	GtkTextIter begin, end;
	gtk_text_buffer_get_start_iter(buffer, &begin);
	gtk_text_buffer_get_end_iter(buffer, &end);
	gchar *descr = gtk_text_buffer_get_text(buffer, &begin, &end, FALSE);
	size_t len = strlen(descr);
	if (len == 0) {
		free(descr);
		return NULL;
	} else if (len > 2000) {	// FIXME
		alert(GTK_MESSAGE_ERROR, "Abusing event description");
		free(descr);
		with_error(0, "Descr too long") return NULL;
	} else {
		for (unsigned c=0; c<len; c++) if (descr[c] == '\n') descr[c] = ' ';
		return descr;
	}
}

static void send_cb(GtkToolButton *button, gpointer user_data)
{
	(void)button;
	assert(! is_error());
	struct editor *e = (struct editor *)user_data;
	// build a new header
	struct header *h = header_new();
	on_error return;
	do {
		char date_str[4+1+2+1+2+1+2+1+2+1];
		struct cal_date date;
		if_fail (header_add_field(h, SCAMBIO_TYPE_FIELD, SCAMBIO_CAL_TYPE)) break;
		// From
		if_fail (cal_date_ctor_from_input(&date, gtk_entry_get_text(GTK_ENTRY(e->start_entry)))) break;
		if (! cal_date_is_set(&date)) {
			alert(GTK_MESSAGE_ERROR, "You must enter a 'From' date");
			break;
		}
		if_fail (cal_date_to_str(&date, date_str, sizeof(date_str))) break;
		if_fail (header_add_field(h, SCAMBIO_START, date_str)) break;
		// To
		if_fail (cal_date_ctor_from_input(&date, gtk_entry_get_text(GTK_ENTRY(e->stop_entry)))) break;
		if (cal_date_is_set(&date)) {
			if_fail (cal_date_to_str(&date, date_str, sizeof(date_str))) break;
			if_fail (header_add_field(h, SCAMBIO_STOP, date_str)) break;
		}
		char *descr = get_serial_text(e->descr_buffer);
		on_error break;
		if (descr) header_add_field(h, SCAMBIO_DESCR_FIELD, descr);
		gint f = gtk_combo_box_get_active(GTK_COMBO_BOX(e->folder_combo));
		if (f == -1) {
			alert(GTK_MESSAGE_ERROR, "You must choose a folder");
			break;
		}
		struct cal_folder *cf;
		LIST_FOREACH(cf, &cal_folders, entry) {
			if (0 == f--) break;
		}
		assert(cf);
		debug("sending patch");
		mdir_patch_request(cf->mdir, MDIR_ADD, h);
	} while (0);
	header_del(h);
	// TODO : if e->replaced, request removal
	unless_error editor_del(e);
}

/*
 * Build the view
 */

GtkWidget *make_edit_window(struct cal_folder *default_cf, struct cal_date *start, struct cal_date *stop, char const *descr, struct cal_event *replaced)
{
	struct editor *editor = malloc(sizeof(*editor));
	if (! editor) with_error(ENOMEM, "malloc editor") return NULL;
	editor->replaced = replaced;

	editor->window = make_window(NULL);

	// First the combo to choose the folder from
	editor->folder_combo = gtk_combo_box_new_text();
	struct cal_folder *cf;
	int i = 0;
	LIST_FOREACH(cf, &cal_folders, entry) {
		gtk_combo_box_append_text(GTK_COMBO_BOX(editor->folder_combo), cf->name);
		if (cf == default_cf) gtk_combo_box_set_active(GTK_COMBO_BOX(editor->folder_combo), i);
		i++;
	}
	GtkWidget *folder_hbox = make_labeled_hbox("Folder :", editor->folder_combo);

	// Then two date editors (text inputs), the second one being optional
	GtkWidget *table = gtk_table_new(2, 2, FALSE);
	GtkWidget *start_label = gtk_label_new("From :");
	gtk_table_attach(GTK_TABLE(table), start_label, 0, 1, 0, 1, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 1, 1);
	editor->start_entry = gtk_entry_new_with_max_length(100);
	gtk_entry_set_text(GTK_ENTRY(editor->start_entry), start->str);
	gtk_table_attach(GTK_TABLE(table), editor->start_entry, 1, 2, 0, 1, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 1, 1);
	GtkWidget *stop_label = gtk_label_new("To :");
	gtk_table_attach(GTK_TABLE(table), stop_label, 0, 1, 1, 2, GTK_SHRINK|GTK_FILL, GTK_SHRINK|GTK_FILL, 1, 1);
	editor->stop_entry = gtk_entry_new_with_max_length(100);
	gtk_entry_set_text(GTK_ENTRY(editor->stop_entry), stop->str);
	gtk_table_attach(GTK_TABLE(table), editor->stop_entry, 1, 2, 1, 2, GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 1, 1);
	
	// Then a text editor for the description
	GtkWidget *descr_text = gtk_text_view_new();
	editor->descr_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(descr_text));
	gtk_text_buffer_set_text(editor->descr_buffer, descr, -1);
	
	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	GtkToolItem *button_cancel = gtk_tool_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_cancel, -1);
	g_signal_connect(G_OBJECT(button_cancel), "clicked", G_CALLBACK(close_cb), editor);
	GtkToolItem *button_ok = gtk_tool_button_new_from_stock(GTK_STOCK_APPLY);
	gtk_toolbar_insert(GTK_TOOLBAR(toolbar), button_ok, -1);
	g_signal_connect(G_OBJECT(button_ok), "clicked", G_CALLBACK(send_cb), editor);

	GtkWidget *vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(vbox), folder_hbox);
	gtk_container_add(GTK_CONTAINER(vbox), table);
	gtk_container_add(GTK_CONTAINER(vbox), descr_text);
	gtk_container_add(GTK_CONTAINER(vbox), toolbar);
	gtk_box_set_child_packing(GTK_BOX(vbox), toolbar, FALSE, TRUE, 1, GTK_PACK_END);
	gtk_box_set_child_packing(GTK_BOX(vbox), folder_hbox, FALSE, TRUE, 1, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(vbox), table, FALSE, TRUE, 1, GTK_PACK_START);
	gtk_box_set_child_packing(GTK_BOX(vbox), descr_text, TRUE, TRUE, 1, GTK_PACK_START);

	gtk_container_add(GTK_CONTAINER(editor->window), vbox);
	return editor->window;
}

