AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = sc_copy

sc_copy_SOURCES = \
	sc_copy.c

sc_copy_LDADD = ../lib/libscambio.la ../commons/libcommons.la

