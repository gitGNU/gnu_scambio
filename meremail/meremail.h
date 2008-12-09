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
#ifndef MEREMAIL_H_081007
#define MEREMAIL_H_081007

#include "merelib.h"
#include "scambio.h"
#include "scambio/mdir.h"
#include "scambio/header.h"
#include "auth.h"
#include "scambio/channel.h"
#include "maildir.h"

extern struct chn_cnx ccnx;
extern struct mdir_user *user;

GtkWidget *make_list_window(char const *folder);
GtkWidget *make_folder_window(char const *parent);
GtkWidget *make_mail_window(struct msg *);
GtkWidget *make_compose_window(char const *from, char const *to, char const *subject);

#endif
