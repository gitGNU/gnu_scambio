AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = sc_filec

sc_filec_SOURCES = \
	sc_filec.c

sc_filec_LDADD = ../lib/libscambio.la ../commons/libcommons.la

