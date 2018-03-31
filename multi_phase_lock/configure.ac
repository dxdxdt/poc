AC_INIT([poc-multi_phase_lock], [0.0.0], [support@fixstudio.pro])

# 기본 컴파일러 옵션 (-O2 -g) 제거
: ${CFLAGS=""}
: ${CXXFLAGS=""}

AM_INIT_AUTOMAKE([1.0 subdir-objects])

AC_CANONICAL_HOST

AC_CONFIG_MACRO_DIR([m4])

AC_LANG([C++])

AC_PROG_CXX

AC_ARG_ENABLE(debug,
AS_HELP_STRING([--enable-debug],
               [enable debugging, default: no]),
[case "${enableval}" in
             yes) debug=true ;;
             no)  debug=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
esac],
[debug=false])
AM_CONDITIONAL(DEBUG, test x"$debug" = x"true")
AC_ARG_ENABLE(profile,
AS_HELP_STRING([--enable-profile],
               [enable profile (and also debug), default: no]),
[case "${enableval}" in
             yes) profile=true ;;
             no)  profile=false ;;
             *)   AC_MSG_ERROR([bad value ${enableval} for --enable-profile]) ;;
esac],
[profile=false])
AM_CONDITIONAL(PROFILING, test x"$profile" = x"true")
AM_COND_IF(PROFILING, [AM_CONDITIONAL(DEBUG, true)])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
