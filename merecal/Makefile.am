AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -I$(top_srcdir)/merelib -D_GNU_SOURCE $(GTKS_CFLAGS) @MAEMO_CPPFLAGS@

dbusdir=$(datadir)/dbus-1/services
desktopdir=$(datadir)/applications/hildon
icondir=$(datadir)/icons/hicolor/26x26/

bin_PROGRAMS = merecal
dbus_DATA=org.maemo.merecal.service
desktop_DATA=merecal.desktop
icon_DATA=merecal.png

merecal_SOURCES = \
	merecal.c \
	cal_view.c \
	edit_view.c

merecal_LDADD = ../merelib/libgui.la ../lib/libscambio.la ../commons/libcommons.la $(GTKS_LIBS)

