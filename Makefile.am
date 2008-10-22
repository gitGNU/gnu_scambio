AUTOMAKE_OPTIONS = foreign
SUBDIRS = commons lib mdird mdirc filed smtpd merelib meremail merecal doc

.PHONY: cscope loc

cscope:
	cd $(top_srcdir) && cscope -Rb $(CPPFLAGS)

loc:
	@echo `find $(top_srcdir) -name '*.c' -o -name '*.h' | xargs cat | wc -l` lines

distclean-local:
	rm -f cscope.out core

EXTRA_DIST: COPYING
