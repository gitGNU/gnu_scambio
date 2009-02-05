AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE
AM_YFLAGS = -d

pkgsysconfdir=$(sysconfdir)/@PACKAGE@
docdir=$(prefix)/share/doc/@PACKAGE@

sbin_PROGRAMS   = sc_stribution
pkgsysconf_DATA = stribution.conf-dist
doc_SCRIPTS     = sc_stribution-dist

sc_stribution_SOURCES = \
	strib_cache.c \
	strib_lexer.l \
	strib_parser.y \
	stribution.c

sc_stribution_LDADD = ../lib/libscambio.la ../commons/libcommons.la

