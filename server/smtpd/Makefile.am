AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/client/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = smtpd

smtpd_SOURCES = \
	smtpd.c \
	queries.c \
	parse.c

smtpd_LDADD = ../../client/lib/libmdir.la ../../commons/libcommons.la
