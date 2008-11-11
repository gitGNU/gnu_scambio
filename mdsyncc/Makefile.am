AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = sc_mdsyncc

sc_mdsyncc_SOURCES = \
	mdsyncc.c \
	command.c \
	connecter.c \
	reader.c \
	writer.c \
	hide.c

sc_mdsyncc_LDADD = ../lib/libmdir.la ../commons/libcommons.la

