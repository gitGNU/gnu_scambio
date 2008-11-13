AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

sbin_PROGRAMS = sc_merefs
sysconf_DATA  = sc_merefs.conf-dist
doc_SCRIPTS   = sc_merefs-dist

sc_merefs_SOURCES = \
	merefs.c \
	file.c \
	readmdir.c \
	local.c

sc_merefs_LDADD = ../lib/libscambio.la ../commons/libcommons.la

