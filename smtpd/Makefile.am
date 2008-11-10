AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = sc_smtpd

sc_smtpd_SOURCES = \
	smtpd.c \
	queries.c \
	parse.c

sc_smtpd_LDADD = ../lib/libmdir.la ../commons/libcommons.la
