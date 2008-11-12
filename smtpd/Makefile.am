AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

sbin_PROGRAMS = sc_smtpd
sysconf_DATA  = sc_smtpd.conf-dist
doc_SCRIPTS   = sc_smtpd-dist

sc_smtpd_SOURCES = \
	smtpd.c \
	queries.c \
	parse.c

sc_smtpd_LDADD = ../lib/libscambio.la ../commons/libcommons.la
