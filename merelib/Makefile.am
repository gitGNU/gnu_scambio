AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE -DICONDIR=$(datadir)/@GTK_ICONDIR@ $(GTKS_CFLAGS) @MAEMO_CPPFLAGS@

noinst_LTLIBRARIES = libgui.la

libgui_la_SOURCES = merelib.c

