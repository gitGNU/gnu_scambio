AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE
#AM_YFLAGS = -d

pkgsysconfdir=$(sysconfdir)/@PACKAGE@
docdir=$(prefix)/share/doc/@PACKAGE@

sbin_PROGRAMS   = sc_mdsyncd
pkgsysconf_DATA = mdsyncd.conf-dist
doc_SCRIPTS     = sc_mdsyncd-dist

sc_mdsyncd_SOURCES = \
	mdsyncd.c \
	queries.c \
	sub.c

sc_mdsyncd_LDADD = ../lib/libscambio.la ../commons/libcommons.la

