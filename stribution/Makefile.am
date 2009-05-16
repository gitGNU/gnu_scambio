AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE
AM_YFLAGS = -d

pkgsysconfdir=$(sysconfdir)/@PACKAGE@
docdir=$(prefix)/share/doc/@PACKAGE@

sbin_PROGRAMS   = sc_stribution

sc_stribution_SOURCES = \
	main.c main.h \
	strib_cache.c strib_cache.h \
	strib_parser.y \
	strib_lexer.l \
	stribution.c stribution.h

sc_stribution_LDADD = ../lib/libscambio.la ../commons/libcommons.la

