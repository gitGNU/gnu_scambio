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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <pwd.h>
#include <grp.h>
#include "scambio.h"
#include "daemon.h"
#include "misc.h"

static void switch_user(void)
{
	char const *user = conf_get_str("SC_RUNASUSER");
	char const *group = conf_get_str("SC_RUNASGROUP");
	if (group) {
		debug("Switching to group '%s'", group);
		errno = 0;
		struct group *grp = getgrnam(group);
		if (! grp) with_error(errno, "Cannot get gid for '%s'", group) return;
		if (setgid(grp->gr_gid) < 0) with_error(errno, "setgid(%d)", (int)grp->gr_gid) return;
	}
	if (user) {
		debug("Switching to user '%s'", user);
		errno = 0;
		struct passwd *pwd = getpwnam(user);
		if (! pwd) with_error(errno, "Cannot get uid for '%s'", user) return;
		if (setuid(pwd->pw_uid) < 0) with_error(errno, "setuid(%d)", (int)pwd->pw_uid) return;
	}
}

static void make_pidfile(void)
{
	char const *pidfile = conf_get_str("PIDFILE");
	if (! pidfile) {
		debug("Not creating a pidfile");
		return;
	}
	debug("Create pidfile as '%s'", pidfile);
	int fd = creat(pidfile, 0644);
	if (fd < 0) with_error(errno, "open(%s)", pidfile) return;
	char pidstr[20];
	snprintf(pidstr, sizeof(pidstr), "%lld\n", (long long)getpid());
	Write_strs(fd, pidstr, NULL);
	(void)close(fd);
}

void daemonize(char const *log_ident)
{
	pid_t pid;
	(void)umask(0);
	if ((pid = fork()) < 0) with_error(errno, "fork") return;
	if (pid != 0) exit(0);
	if (setsid() < 0) with_error(errno, "setsid") return;
	struct sigaction sa = { .sa_handler = SIG_IGN, .sa_flags = 0, };
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGHUP, &sa, NULL) < 0) with_error(errno, "sigaction") return;
	if ((pid = fork()) < 0) with_error(errno, "fork[2]") return;
	if (pid != 0) exit(0);
	if (chdir("/") < 0) with_error(errno, "chdir(/)") return;
	for (int i=0; i<=2; i++) {
		if (close(i) < 0) with_error(errno, "close(%d)", i) return;
		if (open("/dev/null", O_RDWR) != i) with_error(errno, "open(/dev/null)[%d] failed or did not return the lowest filedescr", i) return;
	}
	openlog(log_ident, LOG_CONS, LOG_DAEMON);
	if_fail (make_pidfile()) return;
	if_fail (switch_user()) return;
}

