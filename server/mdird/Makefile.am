AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/commons -D_GNU_SOURCE
AM_YFLAGS = -d

SUBDIRS = . checks

bin_PROGRAMS = mdird

mdird_SOURCES = \
	strib_parser.y \
	strib_lexer.l \
	stribution.c \
	strib_cache.c \
	mdird.c \
	queries.c \
	jnl.c \
	sub.c

mdird_LDADD = @LEXLIB@ ../../commons/libcommons.la
