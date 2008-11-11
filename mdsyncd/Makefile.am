AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE
AM_YFLAGS = -d

bin_PROGRAMS = sc_mdsyncd

sc_mdsyncd_SOURCES = \
	mdsyncd.c \
	queries.c \
	sub.c

sc_mdsyncd_LDADD = ../lib/libmdir.la ../commons/libcommons.la

