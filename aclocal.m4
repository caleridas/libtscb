AC_DEFUN([ACX_PTHREAD], [
AC_REQUIRE([AC_CANONICAL_HOST])
AC_LANG_SAVE
AC_LANG_C
acx_pthread_ok=no

# We used to check for pthread.h first, but this fails if pthread.h
# requires special compiler flags (e.g. on True64 or Sequent).
# It gets checked for in the link test anyway.

# First of all, check if the user has set any of the PTHREAD_LIBS,
# etcetera environment variables, and if threads linking works using
# them:
if test x"$PTHREAD_LIBS$PTHREAD_CFLAGS" != x; then
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        AC_MSG_CHECKING([for pthread_join in LIBS=$PTHREAD_LIBS with CFLAGS=$PTHREAD_CFLAGS])
        AC_TRY_LINK_FUNC(pthread_join, acx_pthread_ok=yes)
        AC_MSG_RESULT($acx_pthread_ok)
        if test x"$acx_pthread_ok" = xno; then
                PTHREAD_LIBS=""
                PTHREAD_CFLAGS=""
        fi
        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"
fi

# We must check for the threads library under a number of different
# names; the ordering is very important because some systems
# (e.g. DEC) have both -lpthread and -lpthreads, where one of the
# libraries is broken (non-POSIX).

# Create a list of thread flags to try.  Items starting with a "-" are
# C compiler flags, and other items are library names, except for "none"
# which indicates that we try without any flags at all.

acx_pthread_flags="pthreads none -Kthread -kthread lthread -pthread -pthreads -mthreads pthread --thread-safe -mt"

# The ordering *is* (sometimes) important.  Some notes on the
# individual items follow:

# pthreads: AIX (must check this before -lpthread)
# none: in case threads are in libc; should be tried before -Kthread and
#       other compiler flags to prevent continual compiler warnings
# -Kthread: Sequent (threads in libc, but -Kthread needed for pthread.h)
# -kthread: FreeBSD kernel threads (preferred to -pthread since SMP-able)
# lthread: LinuxThreads port on FreeBSD (also preferred to -pthread)
# -pthread: Linux/gcc (kernel threads), BSD/gcc (userland threads)
# -pthreads: Solaris/gcc
# -mthreads: Mingw32/gcc, Lynx/gcc
# -mt: Sun Workshop C (may only link SunOS threads [-lthread], but it
#      doesn't hurt to check since this sometimes defines pthreads too;
#      also defines -D_REENTRANT)
# pthread: Linux, etcetera
# --thread-safe: KAI C++

case "${host_cpu}-${host_os}" in
        *solaris*)

        # On Solaris (at least, for some versions), libc contains stubbed
        # (non-functional) versions of the pthreads routines, so link-based
        # tests will erroneously succeed.  (We need to link with -pthread or
        # -lpthread.)  (The stubs are missing pthread_cleanup_push, or rather
        # a function called by this macro, so we could check for that, but
        # who knows whether they'll stub that too in a future libc.)  So,
        # we'll just look for -pthreads and -lpthread first:

        acx_pthread_flags="-pthread -pthreads pthread -mt $acx_pthread_flags"
        ;;
esac

if test x"$acx_pthread_ok" = xno; then
for flag in $acx_pthread_flags; do

        case $flag in
                none)
                AC_MSG_CHECKING([whether pthreads work without any flags])
                ;;

                -*)
                AC_MSG_CHECKING([whether pthreads work with $flag])
                PTHREAD_CFLAGS="$flag"
                ;;

                *)
                AC_MSG_CHECKING([for the pthreads library -l$flag])
                PTHREAD_LIBS="-l$flag"
                ;;
        esac

        save_LIBS="$LIBS"
        save_CFLAGS="$CFLAGS"
        LIBS="$PTHREAD_LIBS $LIBS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Check for various functions.  We must include pthread.h,
        # since some functions may be macros.  (On the Sequent, we
        # need a special flag -Kthread to make this header compile.)
        # We check for pthread_join because it is in -lpthread on IRIX
        # while pthread_create is in libc.  We check for pthread_attr_init
        # due to DEC craziness with -lpthreads.  We check for
        # pthread_cleanup_push because it is one of the few pthread
        # functions on Solaris that doesn't have a non-functional libc stub.
        # We try pthread_create on general principles.
        AC_TRY_LINK([#include <pthread.h>],
                    [pthread_t th; pthread_join(th, 0);
                     pthread_attr_init(0); pthread_cleanup_push(0, 0);
                     pthread_create(0,0,0,0); pthread_cleanup_pop(0); ],
                    [acx_pthread_ok=yes])

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"

        AC_MSG_RESULT($acx_pthread_ok)
        if test "x$acx_pthread_ok" = xyes; then
                break;
        fi

        PTHREAD_LIBS=""
        PTHREAD_CFLAGS=""
done
fi

# Various other checks:
if test "x$acx_pthread_ok" = xyes; then
        save_LIBS="$LIBS"
        LIBS="$PTHREAD_LIBS $LIBS"
        save_CFLAGS="$CFLAGS"
        CFLAGS="$CFLAGS $PTHREAD_CFLAGS"

        # Detect AIX lossage: threads are created detached by default
        # and the JOINABLE attribute has a nonstandard name (UNDETACHED).
        AC_MSG_CHECKING([for joinable pthread attribute])
        AC_TRY_LINK([#include <pthread.h>],
                    [int attr=PTHREAD_CREATE_JOINABLE;],
                    ok=PTHREAD_CREATE_JOINABLE, ok=unknown)
        if test x"$ok" = xunknown; then
                AC_TRY_LINK([#include <pthread.h>],
                            [int attr=PTHREAD_CREATE_UNDETACHED;],
                            ok=PTHREAD_CREATE_UNDETACHED, ok=unknown)
        fi
        if test x"$ok" != xPTHREAD_CREATE_JOINABLE; then
                AC_DEFINE(PTHREAD_CREATE_JOINABLE, $ok,
                          [Define to the necessary symbol if this constant
                           uses a non-standard name on your system.])
        fi
        AC_MSG_RESULT(${ok})
        if test x"$ok" = xunknown; then
                AC_MSG_WARN([we do not know how to create joinable pthreads])
        fi

        AC_MSG_CHECKING([if more special flags are required for pthreads])
        flag=no
        case "${host_cpu}-${host_os}" in
                *-aix* | *-freebsd*)     flag="-D_THREAD_SAFE";;
                *solaris* | *-osf* | *-hpux*) flag="-D_REENTRANT";;
        esac
        AC_MSG_RESULT(${flag})
        if test "x$flag" != xno; then
                PTHREAD_CFLAGS="$flag $PTHREAD_CFLAGS"
        fi

        LIBS="$save_LIBS"
        CFLAGS="$save_CFLAGS"

        # More AIX lossage: must compile with cc_r
        AC_CHECK_PROG(PTHREAD_CC, cc_r, cc_r, ${CC})
else
        PTHREAD_CC="$CC"
fi

AC_SUBST(PTHREAD_LIBS)
AC_SUBST(PTHREAD_CFLAGS)
AC_SUBST(PTHREAD_CC)

# Finally, execute ACTION-IF-FOUND/ACTION-IF-NOT-FOUND:
if test x"$acx_pthread_ok" = xyes; then
        ifelse([$1],,AC_DEFINE(HAVE_PTHREAD,1,[Define if you have POSIX threads libraries and header files.]),[$1])
        :
else
        acx_pthread_ok=no
        $2
fi
AC_LANG_RESTORE
])dnl ACX_PTHREAD

# ===========================================================================
#          http://www.nongnu.org/autoconf-archive/ax_boost_base.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_BOOST_BASE([MINIMUM-VERSION], [ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
#
# DESCRIPTION
#
#   Test for the Boost C++ libraries of a particular version (or newer)
#
#   If no path to the installed boost library is given the macro searchs
#   under /usr, /usr/local, /opt and /opt/local and evaluates the
#   $BOOST_ROOT environment variable. Further documentation is available at
#   <http://randspringer.de/boost/index.html>.
#
#   This macro calls:
#
#     AC_SUBST(BOOST_CPPFLAGS) / AC_SUBST(BOOST_LDFLAGS)
#
#   And sets:
#
#     HAVE_BOOST
#
# LICENSE
#
#   Copyright (c) 2008 Thomas Porschberg <thomas@randspringer.de>
#   Copyright (c) 2009 Peter Adolphs
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

AC_DEFUN([AX_BOOST_BASE],
[
AC_ARG_WITH([boost],
  [AC_HELP_STRING([--with-boost@<:@=ARG@:>@],
    [use Boost library from a standard location (ARG=yes),
     from the specified location (ARG=<path>),
     or disable it (ARG=no)
     @<:@ARG=yes@:>@ ])],
    [
    if test "$withval" = "no"; then
        want_boost="no"
    elif test "$withval" = "yes"; then
        want_boost="yes"
        ac_boost_path=""
    else
        want_boost="yes"
        ac_boost_path="$withval"
    fi
    ],
    [want_boost="yes"])


AC_ARG_WITH([boost-libdir],
        AS_HELP_STRING([--with-boost-libdir=LIB_DIR],
        [Force given directory for boost libraries. Note that this will overwrite library path detection, so use this parameter only if default library detection fails and you know exactly where your boost libraries are located.]),
        [
        if test -d $withval
        then
                ac_boost_lib_path="$withval"
        else
                AC_MSG_ERROR(--with-boost-libdir expected directory name)
        fi
        ],
        [ac_boost_lib_path=""]
)

if test "x$want_boost" = "xyes"; then
    boost_lib_version_req=ifelse([$1], ,1.20.0,$1)
    boost_lib_version_req_shorten=`expr $boost_lib_version_req : '\([[0-9]]*\.[[0-9]]*\)'`
    boost_lib_version_req_major=`expr $boost_lib_version_req : '\([[0-9]]*\)'`
    boost_lib_version_req_minor=`expr $boost_lib_version_req : '[[0-9]]*\.\([[0-9]]*\)'`
    boost_lib_version_req_sub_minor=`expr $boost_lib_version_req : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
    if test "x$boost_lib_version_req_sub_minor" = "x" ; then
        boost_lib_version_req_sub_minor="0"
        fi
    WANT_BOOST_VERSION=`expr $boost_lib_version_req_major \* 100000 \+  $boost_lib_version_req_minor \* 100 \+ $boost_lib_version_req_sub_minor`
    AC_MSG_CHECKING(for boostlib >= $boost_lib_version_req)
    succeeded=no

    libsubdir="lib"
    if test "$(uname -m)" = "x86_64"; then
        libsubdir="lib64"
    fi

    dnl first we check the system location for boost libraries
    dnl this location ist chosen if boost libraries are installed with the --layout=system option
    dnl or if you install boost with RPM
    if test "$ac_boost_path" != ""; then
        BOOST_LDFLAGS="-L$ac_boost_path/$libsubdir"
        BOOST_CPPFLAGS="-I$ac_boost_path/include"
    elif test "$cross_compiling" != yes; then
        for ac_boost_path_tmp in /usr /usr/local /opt /opt/local ; do
            if test -d "$ac_boost_path_tmp/include/boost" && test -r "$ac_boost_path_tmp/include/boost"; then
                BOOST_LDFLAGS="-L$ac_boost_path_tmp/$libsubdir"
                BOOST_CPPFLAGS="-I$ac_boost_path_tmp/include"
                break;
            fi
        done
    fi

    dnl overwrite ld flags if we have required special directory with
    dnl --with-boost-libdir parameter
    if test "$ac_boost_lib_path" != ""; then
       BOOST_LDFLAGS="-L$ac_boost_lib_path"
    fi

    CPPFLAGS_SAVED="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
    export CPPFLAGS

    LDFLAGS_SAVED="$LDFLAGS"
    LDFLAGS="$LDFLAGS $BOOST_LDFLAGS"
    export LDFLAGS

    AC_REQUIRE([AC_PROG_CXX])
    AC_LANG_PUSH(C++)
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    @%:@include <boost/version.hpp>
    ]], [[
    #if BOOST_VERSION >= $WANT_BOOST_VERSION
    // Everything is okay
    #else
    #  error Boost version is too old
    #endif
    ]])],[
        AC_MSG_RESULT(yes)
    succeeded=yes
    found_system=yes
        ],[
        ])
    AC_LANG_POP([C++])



    dnl if we found no boost with system layout we search for boost libraries
    dnl built and installed without the --layout=system option or for a staged(not installed) version
    if test "x$succeeded" != "xyes"; then
        _version=0
        if test "$ac_boost_path" != ""; then
            if test -d "$ac_boost_path" && test -r "$ac_boost_path"; then
                for i in `ls -d $ac_boost_path/include/boost-* 2>/dev/null`; do
                    _version_tmp=`echo $i | sed "s#$ac_boost_path##" | sed 's/\/include\/boost-//' | sed 's/_/./'`
                    V_CHECK=`expr $_version_tmp \> $_version`
                    if test "$V_CHECK" = "1" ; then
                        _version=$_version_tmp
                    fi
                    VERSION_UNDERSCORE=`echo $_version | sed 's/\./_/'`
                    BOOST_CPPFLAGS="-I$ac_boost_path/include/boost-$VERSION_UNDERSCORE"
                done
            fi
        else
            if test "$cross_compiling" != yes; then
                for ac_boost_path in /usr /usr/local /opt /opt/local ; do
                    if test -d "$ac_boost_path" && test -r "$ac_boost_path"; then
                        for i in `ls -d $ac_boost_path/include/boost-* 2>/dev/null`; do
                            _version_tmp=`echo $i | sed "s#$ac_boost_path##" | sed 's/\/include\/boost-//' | sed 's/_/./'`
                            V_CHECK=`expr $_version_tmp \> $_version`
                            if test "$V_CHECK" = "1" ; then
                                _version=$_version_tmp
                                best_path=$ac_boost_path
                            fi
                        done
                    fi
                done

                VERSION_UNDERSCORE=`echo $_version | sed 's/\./_/'`
                BOOST_CPPFLAGS="-I$best_path/include/boost-$VERSION_UNDERSCORE"
                if test "$ac_boost_lib_path" = ""
                then
                   BOOST_LDFLAGS="-L$best_path/$libsubdir"
                fi
            fi

            if test "x$BOOST_ROOT" != "x"; then
                if test -d "$BOOST_ROOT" && test -r "$BOOST_ROOT" && test -d "$BOOST_ROOT/stage/$libsubdir" && test -r "$BOOST_ROOT/stage/$libsubdir"; then
                    version_dir=`expr //$BOOST_ROOT : '.*/\(.*\)'`
                    stage_version=`echo $version_dir | sed 's/boost_//' | sed 's/_/./g'`
                        stage_version_shorten=`expr $stage_version : '\([[0-9]]*\.[[0-9]]*\)'`
                    V_CHECK=`expr $stage_version_shorten \>\= $_version`
                    if test "$V_CHECK" = "1" -a "$ac_boost_lib_path" = "" ; then
                        AC_MSG_NOTICE(We will use a staged boost library from $BOOST_ROOT)
                        BOOST_CPPFLAGS="-I$BOOST_ROOT"
                        BOOST_LDFLAGS="-L$BOOST_ROOT/stage/$libsubdir"
                    fi
                fi
            fi
        fi

        CPPFLAGS="$CPPFLAGS $BOOST_CPPFLAGS"
        export CPPFLAGS
        LDFLAGS="$LDFLAGS $BOOST_LDFLAGS"
        export LDFLAGS

        AC_LANG_PUSH(C++)
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
        @%:@include <boost/version.hpp>
        ]], [[
        #if BOOST_VERSION >= $WANT_BOOST_VERSION
        // Everything is okay
        #else
        #  error Boost version is too old
        #endif
        ]])],[
            AC_MSG_RESULT(yes)
        succeeded=yes
        found_system=yes
            ],[
            ])
        AC_LANG_POP([C++])
    fi

    if test "$succeeded" != "yes" ; then
        if test "$_version" = "0" ; then
            AC_MSG_NOTICE([[We could not detect the boost libraries (version $boost_lib_version_req_shorten or higher). If you have a staged boost library (still not installed) please specify \$BOOST_ROOT in your environment and do not give a PATH to --with-boost option.  If you are sure you have boost installed, then check your version number looking in <boost/version.hpp>. See http://randspringer.de/boost for more documentation.]])
        else
            AC_MSG_NOTICE([Your boost libraries seems to old (version $_version).])
        fi
        # execute ACTION-IF-NOT-FOUND (if present):
        ifelse([$3], , :, [$3])
    else
        AC_SUBST(BOOST_CPPFLAGS)
        AC_SUBST(BOOST_LDFLAGS)
        AC_DEFINE(HAVE_BOOST,,[define if the Boost library is available])
        # execute ACTION-IF-FOUND (if present):
        ifelse([$2], , :, [$2])
    fi

    CPPFLAGS="$CPPFLAGS_SAVED"
    LDFLAGS="$LDFLAGS_SAVED"
fi

])
