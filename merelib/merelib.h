/* Copyright 2008 Cedric Cellier.
 *
 * This file is part of Scambio.
 *
 * Scambio is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Scambio is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Scambio.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef MERELIB_H_081008
#define MERELIB_H_081008

#include <stdbool.h>
#include <gtk/gtk.h>
#ifdef WITH_MAEMO
#include <hildon/hildon-program.h>
#include <gtk/gtkmain.h>
#endif
#include "varbuf.h"
#include "scambio/header.h"
#include "auth.h"

struct mdir_user *user;

void init(char const *app_name, int nb_args, char *args[]);
void destroy_cb(GtkWidget *widget, gpointer data);
void exit_when_closed(GtkWidget *);
/* Type may be one of :
 * GTK_MESSAGE_INFO,
 * GTK_MESSAGE_WARNING,
 * GTK_MESSAGE_QUESTION,
 * GTK_MESSAGE_ERROR,
 * GTK_MESSAGE_OTHER
 */
void alert(GtkMessageType type, char const *fmt, ...)
#ifdef __GNUC__
	__attribute__ ((__format__ (__printf__, 2, 3)))
#endif
;
void alert_error(void);
bool confirm(char const *);
void varbuf_ctor_from_gtk_text_view(struct varbuf *vb, GtkWidget *widget);
void close_cb(GtkToolButton *button, gpointer user_data);	// a simple callback that just deletes the given window
struct chn_cnx;
void wait_all_tx(struct chn_cnx *ccnx, GtkWindow *parent);
enum window_class {
	WC_FOLDERS,
	WC_MSGLIST,
	WC_VIEWER,
	WC_EDITOR,
};
GtkWidget *make_window(enum window_class, void (*cb)(GtkWidget *, gpointer), gpointer);
GtkWidget *make_labeled_hbox(char const *label, GtkWidget *other);
GtkWidget *make_labeled_hboxes(unsigned nb_rows, ...);
GtkWidget *make_toolbar(unsigned nb_buttons, ...);
GtkWidget *make_scrollable(GtkWidget *wdg);
GtkWidget *make_frame(char const *title, GtkWidget *wdg);
GtkWidget *make_expander(char const *title, GtkWidget *wdg);
void empty_container(GtkWidget *container);

#if TRUE != GTK_CHECK_VERSION(2, 14, 0)
GtkWidget *gtk_dialog_get_content_area(GtkDialog *dialog);
#endif

#endif
