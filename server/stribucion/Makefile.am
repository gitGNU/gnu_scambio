AM_CFLAGS = -std=c99 -Wall -W -D_GNU_SOURCE
AM_YFLAGS = -d

SUBDIRS = . checks

noinst_LTLIBRARIES = libstribucion.la

libstribucion_la_SOURCES = \
	header.c \
	conf_parser.y \
	conf_lexer.l \
	conf.c \
	conf_cache.c

libstribucion_la_LIBADD = @LEXLIB@
