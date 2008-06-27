AM_CFLAGS = -std=c99 -Wall -W
AM_CPPFLAGS = -I$(top_srcdir)/commons -I$(top_srcdir)/server/mdird -D_GNU_SOURCE

check_PROGRAMS = test1 test2

test1_SOURCES = test1.c ../header.c
test1_LDADD = ../../../commons/libcommons.la
test2_SOURCES = test2.c \
	../header.c ../stribution.c ../strib_cache.c \
	../strib_lexer.c ../strib_parser.c
test2_LDADD = ../../../commons/libcommons.la

TESTS = test1 test2

test2.c: $(shell pwd)/conf1.sample

$(shell pwd)/conf1.sample:
	ln -fs $(srcdir)/conf1.sample $@

