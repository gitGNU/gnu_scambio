AUTOMAKE_OPTIONS = foreign
SUBDIRS = commons lib mdsyncd mdsyncc filed smtpd tools merefs merenotifier @GTK_SUBDIRS@

.PHONY: cscope loc

doc_DATA = scambio.conf-dist

cscope:
	cd $(top_srcdir) && cscope -Rb $(CPPFLAGS)

loc:
	@echo `find $(top_srcdir) -name '*.c' -o -name '*.h' | xargs cat | wc -l` lines

distclean-local:
	rm -f cscope.out core

EXTRA_DIST: COPYING
