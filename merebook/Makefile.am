AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -I$(top_srcdir)/merelib -D_GNU_SOURCE $(GTKS_CFLAGS) @MAEMO_CPPFLAGS@

dbusdir=$(datadir)/dbus-1/services
desktopdir=$(datadir)/applications/hildon

bin_PROGRAMS = merebook
dbus_DATA=org.happyleptic.merebook.service
desktop_DATA=merebook.desktop

merebook_SOURCES = \
	merebook.c \
	book_view.c \
	contact_view.c

merebook_LDADD = ../merelib/libgui.la ../lib/libscambio.la ../commons/libcommons.la $(GTKS_LIBS)

