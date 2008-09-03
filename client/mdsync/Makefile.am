AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/client/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = mdsync

mdsync_SOURCES = \
	mdsync.c

mdsync_LDADD = ../../client/lib/libmdir.la ../../commons/libcommons.la
