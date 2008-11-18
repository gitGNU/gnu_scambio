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
#ifndef NOTIF_H_081118
#define NOTIF_H_081118

#include <stdbool.h>
#include "scambio/queue.h"

struct notif {
	TAILQ_ENTRY(notif) entry;
	bool new;
	enum sc_notif_type {
		SC_NOTIF_NEW_MAIL,
		SC_NOTIF_NEW_FILE,
		SC_NOTIF_NEW_EVENT,
		SC_NOTIF_ALERT_EVENT,
		SC_NB_NOTIFS,
	} type;
	char descr[1024];
};

struct header;
struct notif *notif_new(enum sc_notif_type, char const *descr);
struct notif *notif_new_from_header(struct header *h);
void notif_del(struct notif *);

extern TAILQ_HEAD(notifs, notif) notifs;

extern struct notif_conf {
	bool display_short;	// weither we should display this event type in the short list
	bool display_long;	// and in the long detailed list
	char const *cmd;	// command to run when we select the notif
} notif_confs[SC_NB_NOTIFS];

void notif_begin(void);
void notif_end(void);

#endif
