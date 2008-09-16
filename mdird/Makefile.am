AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE
AM_YFLAGS = -d

bin_PROGRAMS = mdird

mdird_SOURCES = \
	mdird.c \
	queries.c \
	sub.c

mdird_LDADD = ../lib/libmdir.la ../commons/libcommons.la

