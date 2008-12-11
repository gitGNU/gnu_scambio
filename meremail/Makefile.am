AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -I$(top_srcdir)/merelib -D_GNU_SOURCE $(GTKS_CFLAGS)

dbusdir=$(datadir)/dbus-1/services
desktopdir=$(datadir)/applications/hildon

bin_PROGRAMS = meremail
dbus_DATA=org.happyleptic.meremail.service
desktop_DATA=meremail.desktop

meremail_SOURCES = \
	meremail.c \
	list_view.c \
	folder_view.c \
	mail_view.c \
	compose_view.c \
	maildir.c

meremail_LDADD = ../merelib/libgui.la ../lib/libscambio.la ../commons/libcommons.la $(GTKS_LIBS)

