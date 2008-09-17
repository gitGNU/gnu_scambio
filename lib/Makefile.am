AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

lib_LTLIBRARIES = libmdir.la

libmdir_la_SOURCES = \
	mdir.c \
	jnl.c \
	header.c \
	error.c

libmdir_la_LIBADD = ../commons/libcommons.la

