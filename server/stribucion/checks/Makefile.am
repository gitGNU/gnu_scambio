AM_CPPFLAGS = -I$(top_srcdir)/server/stribucion
AM_CFLAGS = -std=c99 -Wall -W

check_PROGRAMS = test1 test2

test1_SOURCES = test1.c
test1_LDADD = ../libstribucion.la
test2_SOURCES = test2.c
test2_LDADD = ../libstribucion.la

TESTS = test1 test2

test2.c: $(shell pwd)/conf1.sample

$(shell pwd)/conf1.sample:
	ln -fs $(srcdir)/conf1.sample $@

