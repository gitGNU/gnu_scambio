#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(scambio, 0, rixed@free.fr)
AC_CONFIG_SRCDIR([mdsyncd])
AC_CONFIG_HEADER([commons/config.h])
AM_INIT_AUTOMAKE

# Configuration alternatives : configure for no GTK, GTK or GTK+Maemo ?
AC_ARG_WITH([desktop],
	[AS_HELP_STRING([--with-desktop=ARG], [choose between gtk, maemo or no])],
	[],
	[with_desktop=check])

# Checks for programs.
AM_PROG_LEX
AC_PROG_YACC
AM_PROG_LIBTOOL

# Checks for libraries.
AC_CHECK_PTH(2.0.0)
AC_CHECK_LIB(ssl, SHA1)
AC_CHECK_LIB(gnutls, gnutls_fingerprint)
DO_GTKAPPS="yes"
AS_IF(
	[test "x$with_desktop" = xmaemo],
		[PKG_CHECK_MODULES(GTKS, gtk+-2.0 >= 2.2 glib-2.0 >= 2.2 hildon-1 >= 0.18 libosso >= 1.3, [DO_GTKAPPS="maemo"], [AC_MSG_ERROR("cannot found MAEMO desktop libs")])],
	[test "x$with_desktop" = xgtk],
		[PKG_CHECK_MODULES(GTKS, gtk+-2.0 >= 2.2 glib-2.0 >= 2.2, [], [AC_MSG_ERROR("cannot found GTK libs")])],
	[test "x$with_desktop" = xno],
		[DO_GTKAPPS="no"],
	[test "x$with_desktop" = xcheck],
		[PKG_CHECK_MODULES(GTKS, gtk+-2.0 >= 2.2 glib-2.0 >= 2.2, [], [DO_GTKAPPS="no"])])

AS_IF(
	[test "x$DO_GTKAPPS" = xyes],
		[
			AC_SUBST(GTK_SUBDIRS, "merelib meremail merecal")
			AC_SUBST(MAEMO_CPPFLAGS, "")
		],
	[test "x$DO_GTKAPPS" = xmaemo],
		[
			AC_SUBST(GTK_SUBDIRS, "merelib meremail merecal")
			AC_SUBST(MAEMO_CPPFLAGS, "-DWITH_MAEMO")
		],
	[test "x$DO_GTKAPPS" = xno],
		[
			AC_MSG_WARN([will skip GTK applications])
			AC_SUBST(GTK_SUBDIRS, [])
		])

# force it
AC_SUBST(GTK_SUBDIRS, "merelib meremail merecal")
AC_SUBST(MAEMO_CPPFLAGS, "-DWITH_MAEMO")

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h miscmac.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.
AC_CHECK_FUNCS([memset strtol strnstr])

AC_CONFIG_FILES([
	Makefile
	commons/Makefile
	include/Makefile
	lib/Makefile
	mdsyncd/Makefile
	mdsyncc/Makefile
	filed/Makefile
	smtpd/Makefile
	merefs/Makefile
	merelib/Makefile
	meremail/Makefile
	merecal/Makefile
	merenotifier/Makefile
	tools/Makefile
])
AC_OUTPUT
