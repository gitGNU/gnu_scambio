AUTOMAKE_OPTIONS = foreign
SUBDIRS = commons client server doc

.PHONY: cscope

cscope:
	cd $(top_srcdir) && cscope -Rb $(CPPFLAGS)

distclean-local:
	rm -f cscope.out

EXTRA_DIST: COPYING
