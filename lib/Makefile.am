AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

lib_LTLIBRARIES = libscambio.la

libscambio_la_SOURCES = \
	mdir.c \
	jnl.c \
	header.c \
	error.c \
	cmd.c \
	cnx.c \
	channel.c \
	stream.c \
	timetools.c

libscambio_la_LIBADD = ../commons/libcommons.la

