AUTOMAKE_OPTIONS = foreign
SUBDIRS = commons lib mdsyncd mdsyncc filed filec smtpd sendmail tools merefs merenotifier @GTK_SUBDIRS@

.PHONY: cscope loc

docdir=$(prefix)/share/doc/
doc_DATA = scambio.conf-dist

cscope:
	cd $(top_srcdir) && cscope -Rb $(CPPFLAGS)

loc:
	@echo "You spent : "`find $(top_srcdir) -name '*.c' -o -name '*.h' | xargs cat | wc -l` lines

distclean-local:
	rm -f cscope.out core

EXTRA_DIST: COPYING
