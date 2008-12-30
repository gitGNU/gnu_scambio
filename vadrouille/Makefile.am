AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -I$(top_srcdir)/merelib -D_GNU_SOURCE $(GTKS_CFLAGS) $(GTKHTML_CFLAGS) @MAEMO_CPPFLAGS@

dbusdir=$(datadir)/dbus-1/services
desktopdir=$(datadir)/applications/hildon

bin_PROGRAMS = vadrouille
dbus_DATA=org.happyleptic.vadrouille.service
desktop_DATA=vadrouille.desktop

vadrouille_SOURCES = \
	vadrouille.c \
	mdirb.c \
	browser.c \
	msg.c \
	mail.c \
	mail_composer.c \
	calendar.c \
	cal_date.c \
	cal_editor.c \
	contact.c \
	contact_field.c \
	dialog.c \
	bookmark.c

vadrouille_LDADD = ../merelib/libgui.la ../lib/libscambio.la ../commons/libcommons.la $(GTKS_LIBS) $(GTKHTML_LIBS)

