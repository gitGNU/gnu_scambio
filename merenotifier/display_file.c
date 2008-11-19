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
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "scambio.h"
#include "conf.h"
#include "misc.h"
#include "notif.h"
#include "display.h"
/* This version display the notifications in a file
 */

/*
 * Data Definitions
 */

static int fd = -1;

/*
 * Init
 */

void display_begin(void)
{
	char const *home = getenv("HOME");
	if (! home) home = "/tmp";
	char default_file[PATH_MAX];
	snprintf(default_file, sizeof(default_file), "%s/.merenotifier.log", home);
	conf_set_default_str("SC_NOTIFIER_FILE", default_file);

	char const *filename = conf_get_str("SC_NOTIFIER_FILE");
	on_error return;
	fd = open(filename, O_WRONLY|O_APPEND|O_CREAT, 0640);
	if (fd < 0) with_error(errno, "open(%s)", filename) return;
	Write_strs(fd, "merenotifier startup\n", NULL);
}

void display_end(void)
{
	if (fd >= 0) {
		(void)close(fd);
		fd = -1;
	}
}

/*
 * Refresh because the notif list changed
 */

void display_refresh(void)
{
	struct notif *notif;
	TAILQ_FOREACH(notif, &notifs, entry) {
		if (notif->new) {
			debug("new notif");
			if_fail (Write_strs(fd, "New ", notif_type2str(notif->type), " : ", notif->descr, "\n", NULL)) return;
			notif->new = false;
		}
	}
}

