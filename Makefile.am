AUTOMAKE_OPTIONS = foreign
SUBDIRS = commons lib mdird mdirc smtpd meremail doc

.PHONY: cscope

cscope:
	cd $(top_srcdir) && cscope -Rb $(CPPFLAGS)

distclean-local:
	rm -f cscope.out core

EXTRA_DIST: COPYING
