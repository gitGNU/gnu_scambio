AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/include -I$(top_srcdir)/commons -D_GNU_SOURCE

docdir=$(prefix)/share/doc/@PACKAGE@

sbin_PROGRAMS   = sc_sendmail
doc_SCRIPTS     = sc_sendmail-dist

sc_sendmail_SOURCES = \
	sendmail.c \
	forwarder.c \
	crawler.c

sc_sendmail_LDADD = ../lib/libscambio.la ../commons/libcommons.la
