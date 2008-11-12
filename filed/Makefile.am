AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

sbin_PROGRAMS = sc_filed

sc_filed_SOURCES = \
	filed.c

sc_filed_LDADD = ../lib/libscambio.la ../commons/libcommons.la

