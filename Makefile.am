AUTOMAKE_OPTIONS = foreign
SUBDIRS = commons server doc

.PHONY: cscope

cscope:
	cd $(top_srcdir) && cscope -Rb $(CPPFLAGS)

distclean-local:
	rm -f cscope.out
