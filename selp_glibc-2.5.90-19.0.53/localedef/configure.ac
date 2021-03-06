AC_INIT([glibc build-side support], [(see version.h)])
AC_DEFUN([LOCAL_CHECK_TYPE],
	 [AC_CHECK_TYPE([$1],
			[AC_DEFINE(translit([HAVE_$1], [a-z], [A-Z]), [1],
				   [Define if you have the $1 type])],,
			[$2])])
AC_DEFUN([LOCAL_CHECK_FUNC],
	 [AC_CHECK_DECL([$1],
			[AC_DEFINE(translit([HAVE_$1], [a-z], [A-Z]), [1],
				   [Define if you have the $1() function])],,
			[$2])])
AC_DEFUN([LOCAL_CHECK_VAR],
	 [AC_CHECK_DECL([$1],
			[AC_DEFINE(translit([HAVE_$1], [a-z], [A-Z]), [1],
				   [Define if your headers declare $1])],,
			[$2])])
AC_ARG_WITH([glibc],
	    [  --with-glibc=PATH       specify location of glibc sources],
	    [test -f "$with_glibc/version.h" ||
	     AC_MSG_ERROR([--with-glibc must point to the glibc sources])],
	    [AC_MSG_ERROR([missing --with-glibc option])])
AC_CANONICAL_BUILD
AC_CONFIG_HEADERS([config.h])
AC_EXEEXT
AC_PROG_CC
AC_PROG_LN_S
AC_C_BIGENDIAN([AC_DEFINE([BUILD_BYTE_ORDER], [__BIG_ENDIAN],
		          [Define to your build machine's byte order])],
	       [AC_DEFINE([BUILD_BYTE_ORDER], [__LITTLE_ENDIAN])])
AC_CHECK_HEADERS([byteswap.h ctype.h dirent.h endian.h features.h \
		  fcntl.h inttypes.h libintl.h mcheck.h nl_types.h stdbool.h \
		  stdio_ext.h stdint.h time.h wctype.h sys/cdefs.h \
		  sys/mman.h sys/param.h sys/stat.h sys/uio.h])
LOCAL_CHECK_TYPE([off64_t])
LOCAL_CHECK_TYPE([blksize_t])
LOCAL_CHECK_TYPE([int_fast32_t])
LOCAL_CHECK_TYPE([__compar_fn_t], [#include <stdlib.h>])
LOCAL_CHECK_TYPE([comparison_fn_t], [#include <stdlib.h>])
LOCAL_CHECK_FUNC([readdir64], [#include <dirent.h>])
LOCAL_CHECK_FUNC([stat64], [#include <sys/stat.h>])
LOCAL_CHECK_FUNC([ftruncate64], [#include <unistd.h>])
LOCAL_CHECK_FUNC([lseek64], [#include <unistd.h>])
LOCAL_CHECK_FUNC([euidaccess], [#include <unistd.h>])
LOCAL_CHECK_FUNC([open64], [#include <fcntl.h>])
LOCAL_CHECK_FUNC([mmap64], [#include <sys/mman.h>])
dnl HAVE_VPRINTF is used in glibc/argp.
LOCAL_CHECK_FUNC([vprintf], [#include <stdio.h>])
LOCAL_CHECK_FUNC([obstack_printf], [#include <stdio.h>])
LOCAL_CHECK_FUNC([getdelim], [#include <stdio.h>])
LOCAL_CHECK_FUNC([asprintf], [#include <stdio.h>])
LOCAL_CHECK_FUNC([vasprintf], [#include <stdio.h>])
LOCAL_CHECK_FUNC([mempcpy], [#include <string.h>])
LOCAL_CHECK_FUNC([strchrnul], [#include <string.h>])
LOCAL_CHECK_FUNC([strndup], [#include <string.h>])
LOCAL_CHECK_FUNC([stpcpy], [#include <string.h>])
LOCAL_CHECK_FUNC([strsep], [#include <string.h>])
LOCAL_CHECK_VAR([program_invocation_name], [#include <errno.h>])
LOCAL_CHECK_VAR([program_invocation_short_name], [#include <errno.h>])
AC_CHECK_LIB([intl], [gettext])
AC_CHECK_LIB([posix4], [nanosleep])

rm -f glibc && $LN_S $with_glibc glibc
case $build_os in
  solaris*)
    SHELL=/bin/bash
    ;;
esac

AC_OUTPUT([Makefile])
