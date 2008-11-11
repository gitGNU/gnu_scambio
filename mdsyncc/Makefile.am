AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

sbin_PROGRAMS = sc_mdsyncc
sysconf_DATA  = sc_mdsyncc.conf-dist
doc_SCRIPTS   = sc_mdsyncc-dist

sc_mdsyncc_SOURCES = \
	mdsyncc.c \
	command.c \
	connecter.c \
	reader.c \
	writer.c \
	hide.c

sc_mdsyncc_LDADD = ../lib/libmdir.la ../commons/libcommons.la

