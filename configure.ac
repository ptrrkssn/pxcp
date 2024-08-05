AC_PREREQ([2.71])
AC_INIT([PXCP],[0.1.0],[pen@lysator.liu.se],[pxcp],[https://github.com/ptrrkssn/pxcp])

dnl AC_LANG(C)
AC_CONFIG_SRCDIR([pxcp.c])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_AUX_DIR([build-aux])

AC_USE_SYSTEM_EXTENSIONS

AC_PROG_CC
AC_PROG_LN_S
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

# Checks for header files.
AC_CHECK_HEADERS([sys/acl.h acl/libacl.h sys/extattr.h sys/dirent.h sys/xattr.h])
AC_CHECK_HEADERS([zlib.h md5.h skein.h sha256.h sha384.h sha512.h])

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
AC_CHECK_MEMBERS([struct stat.st_flags])

AC_SEARCH_LIBS([acl_get_perm], [acl])

AC_CHECK_FUNCS([acl_get_brand_np acl_get_fd_np acl_get_fd acl_get_file acl_get_link_np acl_set_fd_np acl_set_fd acl_set_file acl_set_link_np acl_get_perm acl_get_perm_np acl_get_entry])
AC_CHECK_FUNCS([acl facl])

AC_CHECK_FUNCS([extattr_list_fd extattr_list_link flistxattr llistxattr listxattr])
AC_CHECK_FUNCS([extattr_get_fd extattr_get_link fgetxattr lgetxattr getxattr])
AC_CHECK_FUNCS([extattr_set_fd extattr_set_link fsetxattr lsetxattr setxattr])
AC_CHECK_FUNCS([extattr_delete_fd extattr_delete_link fremovexattr lremovexattr removexattr])

AC_CHECK_FUNCS([utimensat futimens])
AC_CHECK_FUNCS([fstatat fchownat openat renameat mkdirat fchmodat])
AC_CHECK_FUNCS([funlinkat unlinkat])
AC_CHECK_FUNCS([readlinkat freadlink symlinkat])
AC_CHECK_FUNCS([mkostempsat mkostemps mkostemp mkstemp])
AC_CHECK_FUNCS([chflagsat fchflags lchflags])

AC_MSG_CHECKING([for working getdirentries...])
AC_LINK_IFELSE([AC_LANG_PROGRAM(
[[#include <dirent.h>
char buf[65536];]],[[getdirentries(0, buf, sizeof(buf), 0);]]
)],
[AC_MSG_RESULT([yes])
AC_DEFINE([HAVE_GETDIRENTRIES], [1], [Define if getdirentries exists and is linkable.])],
[AC_MSG_RESULT([no])])

AC_SEARCH_LIBS([adler32_z], [z], [], [
  AC_SEARCH_LIBS([crc32_z], [z])])
  
AC_SEARCH_LIBS([MD5Init], [md], [], [
  AC_SEARCH_LIBS([SKEIN256_Init], [md], [], [
    AC_SEARCH_LIBS([SHA256_Init], [md], [], [
      AC_SEARCH_LIBS([SHA384_Init], [md], [], [
        AC_SEARCH_LIBS([SHA512_Init], [md])])])])])
        
AC_CHECK_FUNCS([adler32_z crc32_z MD5Init SKEIN256_Init SHA256_Init SHA384_Init SHA512_Init])

AC_CHECK_FUNCS([Skein_256_Init])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
