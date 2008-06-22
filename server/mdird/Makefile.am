AM_CFLAGS = -std=c99 -Wall -W -D_GNU_SOURCE
AM_CPPFLAGS = -I$(top_srcdir)/commons
AM_YFLAGS = -d

SUBDIRS = . checks

bin_PROGRAMS = mdird

mdird_SOURCES = \
	header.c \
	strib_parser.y \
	strib_lexer.l \
	stribution.c \
	strib_cache.c \
	mdird.c \
	message.c

mdird_LDADD = @LEXLIB@ ../../commons/libcommons.la

