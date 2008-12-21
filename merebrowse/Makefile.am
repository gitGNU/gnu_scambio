AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -I$(top_srcdir)/merelib -D_GNU_SOURCE $(GTKS_CFLAGS) @MAEMO_CPPFLAGS@

dbusdir=$(datadir)/dbus-1/services
desktopdir=$(datadir)/applications/hildon

bin_PROGRAMS = merebrowse
dbus_DATA=org.happyleptic.merebrowse.service
desktop_DATA=merebrowse.desktop

merebrowse_SOURCES = \
	merebrowse.c \
	mdirb.c \
	browser.c

merebrowse_LDADD = ../merelib/libgui.la ../lib/libscambio.la ../commons/libcommons.la $(GTKS_LIBS)

