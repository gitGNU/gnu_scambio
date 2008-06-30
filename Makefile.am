AUTOMAKE_OPTIONS = foreign
SUBDIRS = commons server doc www

.PHONY: cscope

cscope:
	cd $(top_srcdir) && cscope -Rb $(CPPFLAGS)

distclean-local:
	rm -f cscope.out
