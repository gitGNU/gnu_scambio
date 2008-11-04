AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE
AM_YFLAGS = -d

bin_PROGRAMS = filed

filed_SOURCES = \
	filed.c

filed_LDADD = ../lib/libmdir.la ../commons/libcommons.la

