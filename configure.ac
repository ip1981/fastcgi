#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([fcgi], [0.1.0], [pashev.igor@gmail.com])
AC_CONFIG_SRCDIR([main.c])
AC_CONFIG_HEADERS([config.h])
AM_INIT_AUTOMAKE([subdir-objects foreign dist-xz])
AC_CONFIG_MACRO_DIR([m4])


AC_PROG_CC_C99
AC_USE_SYSTEM_EXTENSIONS
AX_PTHREAD

AC_CHECK_PROGS([XXD], [xxd], [none])
AM_CONDITIONAL([HAVE_XXD], [test x$XXD != xnone])
AS_IF([test x$XXD != xnone],
    [AC_DEFINE([HAVE_XXD], [1], [Define to 1 if have an xxd generated source with license text])])

AC_CHECK_HEADERS([fcgiapp.h], [],
    [AC_MSG_ERROR([Missing the fcgiapp.h header file from the libfcgi library])]
)
AC_CHECK_LIB([fcgi], [FCGX_Init], [],
    [AC_MSG_ERROR([Missing the libfcgi library])]
)


AC_ARG_ENABLE([debug], [AS_HELP_STRING([--enable-debug],
              [Enable debug messages @<:@disable by default@:>@])])
AC_MSG_CHECKING([whether to enable debug])
AS_IF([test x$enable_debug = xyes],
      [ AC_DEFINE([ENABLE_DEBUG], [1], [Set to 1 to enable debug messages])
        AC_MSG_RESULT([yes])
      ],
      [ AC_MSG_RESULT([no])
      ]
)
AM_CONDITIONAL([ENABLE_DEBUG], [test x$enable_debug = xyes])


AC_ARG_ENABLE([cgroups], [AS_HELP_STRING([--enable-cgroups],
              [Enable cgroups REST api @<:@enabled by default on linux@:>@])],
              [], [AS_CASE([$host], [*linux*], [enable_cgroups=yes])])
AC_MSG_CHECKING([whether to enable cgroups])
AS_IF([test x$enable_cgroups = xyes],
      [ AC_DEFINE([ENABLE_CGROUPS], [1], [Set to 1 to enable cgroups])
        AC_MSG_RESULT([yes])
        AC_CHECK_HEADER([libcgroup.h], [], [AC_MSG_ERROR([Missing libcgroup headers])])
        AC_CHECK_LIB([cgroup], [cgroup_walk_tree_begin], [], [AC_MSG_ERROR([Missing the libcgroup library])])
      ],
      [ AC_MSG_RESULT([no])
      ]
)
AM_CONDITIONAL([ENABLE_CGROUPS], [test x$enable_cgroups = xyes])

AC_ARG_ENABLE([uri-prefix], [AS_HELP_STRING([--enable-uri-prefix=/fcgi],
              [REST URI prefix, this prefix will be stripped from the request URI, default is /fcgi])],
              [enable_uri_prefix=$enableval], [enable_uri_prefix=/fcgi])
AC_MSG_CHECKING([for REST URI prefix])
AC_MSG_RESULT([$enable_uri_prefix])
AC_DEFINE_UNQUOTED([URI_PREFIX], ["$enable_uri_prefix"], [REST URI prefix])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

