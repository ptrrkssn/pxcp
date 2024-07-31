AC_PREREQ([2.71])
AC_INIT([PXCP],[0.1.0],[pen@lysator.liu.se],[pxcp],[https://github.com/ptrrkssn/pxcp])

AC_CONFIG_SRCDIR([pxcp.c])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for header files.
AC_CHECK_HEADERS([sys/acl.h acl/libacl.h sys/extattr.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UID_T
AC_TYPE_MODE_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC

AC_CHECK_MEMBERS([struct stat.st_birthtim.tv_sec])
AC_CHECK_MEMBERS([struct stat.st_mtimespec.tv_sec])
AC_CHECK_MEMBERS([struct stat.st_mtim.tv_sec])

AC_SEARCH_LIBS([acl_get_perm], [acl])

AC_CHECK_FUNCS([acl_get_brand_np acl_get_fd_np acl_get_fd acl_get_file acl_get_link_np acl_set_fd_np acl_set_fd acl_set_file acl_set_link_np acl_get_perm acl_get_perm_np])
AC_CHECK_FUNCS([extattr_get_fd])
AC_CHECK_FUNCS([utimensat futimens])
AC_CHECK_FUNCS([fstatat fchownat openat renameat fchmodat])
AC_CHECK_FUNCS([funlinkat unlinkat])
AC_CHECK_FUNCS([readlinkat freadlink symlinkat])
AC_CHECK_FUNCS([getdirentries])
AC_CHECK_FUNCS([mkostempsat mkostemps mkostemp mkstemp])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
