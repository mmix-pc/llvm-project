add_custom_target(libc-locale-tests)

add_libc_test(
  locale_no_environment_test
  SUITE libc-locale-tests
  SRCS
    locale_no_environment_test.cpp
    ${LIBC_SOURCE_DIR}/src/locale/locale_name.cpp
  COMPILE_OPTIONS -DLIBC_TARGET_OS_IS_BAREMETAL
  DEPENDS
    libc.src.locale.locale
    libc.src.__support.CPP.string_view
)

add_libc_test(
  locale_thread_test
  SUITE libc-locale-tests
  SRCS locale_thread_test.cpp
  DEPENDS
    libc.hdr.locale_macros
    libc.src.locale.freelocale
    libc.src.locale.newlocale
    libc.src.locale.setlocale
    libc.src.locale.uselocale
    libc.src.locale.__ctype_get_mb_cur_max
    libc.src.pthread.pthread_create
    libc.src.pthread.pthread_join
    libc.src.wchar.mbrtowc
)

add_libc_test(
  builtin_locale_test
  SUITE libc-locale-tests
  SRCS builtin_locale_test.cpp
  DEPENDS
    libc.config.app_h
    libc.hdr.langinfo_macros
    libc.hdr.locale_macros
    libc.src.__support.CPP.string
    libc.src.__support.libc_errno
    libc.src.langinfo.nl_langinfo
    libc.src.langinfo.nl_langinfo_l
    libc.src.locale.duplocale
    libc.src.locale.freelocale
    libc.src.locale.newlocale
    libc.src.locale.setlocale
    libc.src.locale.uselocale
    libc.src.locale.__ctype_get_mb_cur_max
    libc.src.stdlib.getenv
    libc.src.stdlib.setenv
    libc.src.stdlib.unsetenv
    libc.src.string.strerror_l
    libc.src.uchar.c32rtomb
    libc.src.uchar.mbrtoc32
    libc.src.wchar.mbrtowc
    libc.src.wchar.wcrtomb
    libc.src.wchar.btowc
    libc.src.wchar.wctob
    libc.src.wchar.swprintf
    libc.src.wctype.iswalpha
    libc.src.wctype.towlower
)

add_libc_test(
  locale_test
  SUITE
    libc-locale-tests
  SRCS
    locale_test.cpp
  DEPENDS
    libc.hdr.locale_macros
    libc.src.locale.newlocale
    libc.src.locale.uselocale
    libc.src.locale.freelocale
)

add_libc_test(
  localeconv_test
  SUITE
    libc-locale-tests
  SRCS
    localeconv_test.cpp
  DEPENDS
    libc.hdr.locale_macros
    libc.src.locale.localeconv
)
