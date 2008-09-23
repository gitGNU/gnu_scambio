AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

bin_PROGRAMS = mdirc

mdirc_SOURCES = \
	mdirc.c \
	command.c \
	connecter.c \
	reader.c \
	writer.c \
	hide.c

mdirc_LDADD = ../lib/libmdir.la ../commons/libcommons.la

