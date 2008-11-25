AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

pkgsysconfdir=$(sysconfdir)/@PACKAGE@

bin_PROGRAMS    = sc_mdsyncc
pkgsysconf_DATA = mdsyncc.conf-dist
doc_SCRIPTS     = sc_mdsyncc-dist

sc_mdsyncc_SOURCES = \
	mdsyncc.c \
	command.c \
	connecter.c \
	reader.c \
	writer.c \
	hide.c \
	c2l.c

sc_mdsyncc_LDADD = ../lib/libscambio.la ../commons/libcommons.la

