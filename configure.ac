#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(scambio, 0, rixed@free.fr)
AC_CONFIG_SRCDIR([mdsyncd])
AC_CONFIG_HEADER([commons/config.h])
AM_INIT_AUTOMAKE

# Checks for programs.
AM_PROG_LEX
AC_PROG_YACC
AM_PROG_LIBTOOL

# Checks for libraries.
AC_CHECK_PTH(2.0.0)
AC_CHECK_LIB(ssl, SHA1)
AC_CHECK_LIB(gnutls, gnutls_fingerprint)

# Configuration alternatives : configure for no GTK, GTK or GTK+Maemo ?
DO_GTKAPPS="no"
AC_ARG_WITH([desktop],
	[AS_HELP_STRING([--with-desktop=ARG], [choose between gtk, maemo or no])],
	[
		AS_IF(
			[test "x$with_desktop" = xmaemo],
				[PKG_CHECK_MODULES(GTKS, gtk+-2.0 >= 2.2 glib-2.0 >= 2.2 hildon-1 >= 0.18 libosso >= 1.3, [DO_GTKAPPS="maemo"], [AC_MSG_ERROR("cannot find MAEMO desktop libs")])],
			[test "x$with_desktop" = xgtk],
				[PKG_CHECK_MODULES(GTKS, gtk+-2.0 >= 2.2 glib-2.0 >= 2.2, [DO_GTKAPPS="gtk"], [AC_MSG_ERROR("cannot find GTK libs")])])])

if test "x$DO_GTKAPPS" = xno ; then
	AC_MSG_WARN([will skip GTK applications])
	AC_SUBST(GTK_SUBDIRS, [])
else
	PKG_CHECK_MODULES(GTKHTML, libgtkhtml-3.14, [GTKHTMLVER=314], [
		PKG_CHECK_MODULES(GTKHTML, libgtkgtkhtml-3.8, [GTKHTMLVER=380], [AC_MSG_ERROR("cannot find GTKHTML lib")])
	])
	if test "x$DO_GTKAPPS" = xgtk ; then
		AC_SUBST(GTK_SUBDIRS, "merelib meremail merecal merebook icons")
		AC_SUBST(MAEMO_CPPFLAGS, "")
	else # maemo then
		AC_SUBST(GTK_SUBDIRS, "merelib meremail merecal merebook icons")
		AC_SUBST(MAEMO_CPPFLAGS, "-DWITH_MAEMO")
	fi
fi

AC_SUBST(GTK_ICONDIR, "icons/hicolor/scalable/apps")

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
	filec/Makefile
	smtpd/Makefile
	sendmail/Makefile
	merefs/Makefile
	merelib/Makefile
	meremail/Makefile
	meremail/meremail.desktop
	meremail/org.happyleptic.meremail.service
	merecal/Makefile
	merecal/merecal.desktop
	merecal/org.happyleptic.merecal.service
	merebook/Makefile
	merebook/merebook.desktop
	merebook/org.happyleptic.merebook.service
	merenotifier/Makefile
	tools/Makefile
	icons/Makefile
	icons/26x26/Makefile
	icons/40x40/Makefile
	icons/scalable/Makefile
])
AC_OUTPUT
