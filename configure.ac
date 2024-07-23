
AC_PREREQ([2.72])
AC_INIT([PXCL],[1.0.0],[pen@lysator.liu.se],[pxcp],[https://github.com/ptrrkssn/pxcp])

AC_CONFIG_SRCDIR([pxcp.c])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for header files.
AC_CHECK_HEADERS([sys/acl.h sys/extattr.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC

AC_CHECK_FUNCS([acl acl_get_fd_np extattr_get_fd utimensat])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
