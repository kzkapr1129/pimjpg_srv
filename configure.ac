#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([pimjpg_srv], [1.0], [kazuki04292003@gmail.com])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

# Checks for libraries.
# FIXME: Replace `main' with a function in `-lbcm_host':
AC_CHECK_LIB([bcm_host], [main])
# FIXME: Replace `main' with a function in `-lmmal':
AC_CHECK_LIB([mmal], [main])
# FIXME: Replace `main' with a function in `-lmmal_core':
AC_CHECK_LIB([mmal_core], [main])
# FIXME: Replace `main' with a function in `-lmmal_util':
AC_CHECK_LIB([mmal_util], [main])
# FIXME: Replace `main' with a function in `-lvcos':
AC_CHECK_LIB([vcos], [main])

# Checks for header files.
AC_CHECK_HEADERS([memory.h netinet/in.h stdint.h stdlib.h string.h sys/socket.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_ALLOCA
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset select socket strcasecmp])

AC_CONFIG_FILES([Makefile
                 inc/Makefile
                 src/Makefile])
AC_OUTPUT
