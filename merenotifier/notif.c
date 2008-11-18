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
#include <stdlib.h>
#include <string.h>
#include "scambio.h"
#include "varbuf.h"
#include "scambio/header.h"
#include "notif.h"

/*
 * Data Definitions
 */

struct notif_conf notif_confs[SC_NB_NOTIFS];
struct notifs notifs;

/*
 * Construct
 */

static void notif_ctor(struct notif *notif, enum sc_notif_type type, char const *descr)
{
	notif->new = true;
	notif->type = type;
	snprintf(notif->descr, sizeof(notif->descr), "%s", descr);
	TAILQ_INSERT_HEAD(&notifs, notif, entry);
}

struct notif *notif_new(enum sc_notif_type type, char const *descr)
{
	struct notif *notif = malloc(sizeof(*notif));
	if (! notif) with_error(ENOMEM, "malloc(notif)") return NULL;
	if_fail (notif_ctor(notif, type, descr)) {
		free(notif);
		notif = NULL;
	}
	return notif;
}

static void append_field(struct varbuf *vb, struct header *header, char const *pref, char const *field)
{
	char const *value;
	if_fail (value = header_search(header, field)) return;
	if (! value) return;
	varbuf_append_strs(vb, pref ? pref:value, pref ? value:NULL, NULL);
}

static struct notif *notif_new_from_mail(struct header *header)
{
	struct varbuf vb;
	// Is it a new mail ?
	if (NULL != header_search(header, SC_SENT_DATE)) return NULL;
	if_fail (varbuf_ctor(&vb, 1024, true)) return NULL;
	struct notif *notif = NULL;
	do {
		if_fail (append_field(&vb, header, NULL, SC_FROM_FIELD)) break;
		if_fail (append_field(&vb, header, ": ", SC_DESCR_FIELD)) break;
		if_fail (notif = notif_new(SC_NOTIF_NEW_MAIL, vb.buf)) {
			notif = NULL;
			break;
		}
	} while (0);
	varbuf_dtor(&vb);
	return notif;
}

static struct notif *notif_new_from_cal(struct header *header)
{
	struct varbuf vb;
	if_fail (varbuf_ctor(&vb, 1024, true)) return NULL;
	struct notif *notif = NULL;
	do {
		if_fail (append_field(&vb, header, NULL, SC_START_FIELD)) break;
		if_fail (append_field(&vb, header, " - ", SC_DESCR_FIELD)) break;
		if_fail (notif = notif_new(SC_NOTIF_NEW_EVENT, vb.buf)) {
			notif = NULL;
			break;
		}
	} while (0);
	varbuf_dtor(&vb);
	return notif;
}

static struct notif *notif_new_from_file(struct header *header)
{
	struct varbuf vb;
	if_fail (varbuf_ctor(&vb, 1024, true)) return NULL;
	struct notif *notif = NULL;
	do {
		if_fail (append_field(&vb, header, NULL, SC_NAME_FIELD)) break;
		if_fail (notif = notif_new(SC_NOTIF_NEW_FILE, vb.buf)) {
			notif = NULL;
			break;
		}
	} while (0);
	varbuf_dtor(&vb);
	return notif;
}

struct notif *notif_new_from_header(struct header *header)
{
	char const *type_str;
	if_fail (type_str = header_search(header, SC_TYPE_FIELD)) return NULL;
	if (! type_str) return NULL;
	if (0 == strcmp(type_str, SC_MAIL_TYPE)) return notif_new_from_mail(header);
	if (0 == strcmp(type_str, SC_CAL_TYPE))  return notif_new_from_cal(header);
	if (0 == strcmp(type_str, SC_FILE_TYPE)) return notif_new_from_file(header);
	return NULL;
}

/*
 * Destruct
 */

static void notif_dtor(struct notif *notif)
{
	TAILQ_REMOVE(&notifs, notif, entry);
}

void notif_del(struct notif *notif)
{
	notif_dtor(notif);
	free(notif);
}

/*
 * Init
 */

void notif_begin(void)
{
	TAILQ_INIT(&notifs);
	for (unsigned type = 0; type < sizeof_array(notif_confs); type++) {
		// TODO: use gconf or something more sexy
		notif_confs[type].display_short = true;
		notif_confs[type].display_long = true;
		notif_confs[type].cmd = NULL;
	}
}

void notif_end(void)
{
	struct notif *notif;
	while (NULL != (notif = TAILQ_FIRST(&notifs))) {
		notif_del(notif);
	}
}

