#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(scambio, 0, rixed@free.fr)
AC_CONFIG_SRCDIR([mdird])
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
PKG_CHECK_MODULES(GTKS, gtk+-2.0 >= 2.2 glib-2.0 >= 2.2)
AC_SUBST(GTKS_CFLAGS)
AC_SUBST(GTKS_LIBS)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h unistd.h miscmac.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_CHECK_FUNCS([memset strtol strnstr])

AC_CONFIG_FILES([
	Makefile
	commons/Makefile
	include/Makefile
	lib/Makefile
	mdird/Makefile
	mdirc/Makefile
	filed/Makefile
	smtpd/Makefile
	merelib/Makefile
	meremail/Makefile
	merecal/Makefile
	tools/Makefile
	doc/Makefile
])
AC_OUTPUT
