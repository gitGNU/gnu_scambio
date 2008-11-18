AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = merenotifier

merenotifier_SOURCES = \
	notifier.c

merenotifier_LDADD = ../lib/libscambio.la ../commons/libcommons.la

