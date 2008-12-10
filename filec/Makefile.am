AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

pkgsysconfdir=$(sysconfdir)/@PACKAGE@
docdir=$(prefix)/share/doc/@PACKAGE@

sbin_PROGRAMS   = sc_filec
pkgsysconf_DATA = filec.conf-dist
doc_SCRIPTS     = sc_filec-dist

sc_filec_SOURCES = \
	filec.c

sc_filec_LDADD = ../lib/libscambio.la ../commons/libcommons.la

