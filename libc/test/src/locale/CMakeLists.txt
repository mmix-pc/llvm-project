add_custom_target(libc-locale-tests)

add_libc_test(
  conventions_test
  SUITE libc-locale-tests
  SRCS conventions_test.cpp
  DEPENDS libc.src.locale.conventions libc.src.locale.localeconv
    libc.src.locale.uselocale libc.src.langinfo.nl_langinfo
    libc.src.langinfo.nl_langinfo_l libc.hdr.langinfo_macros
    libc.src.__support.CPP.limits libc.src.__support.libc_errno
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.locale.conventions_test.__build__)
  target_compile_definitions(libc.test.src.locale.conventions_test.__build__ PRIVATE
    CONVENTIONS_ALLOCATION_TEST)
  target_link_options(libc.test.src.locale.conventions_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=free)
endif()

set(character_api_deps
  libc.src.locale.locale_storage libc.src.locale.uselocale
  libc.src.strings.strcasecmp libc.src.strings.strcasecmp_l
  libc.src.strings.strncasecmp libc.src.strings.strncasecmp_l
  libc.src.__support.libc_errno libc.hdr.errno_macros
  libc.hdr.stdio_macros libc.hdr.wchar_macros)
foreach(name isalnum isalpha isblank iscntrl isdigit isgraph islower isprint
             ispunct isspace isupper isxdigit tolower toupper)
  list(APPEND character_api_deps libc.src.ctype.${name} libc.src.ctype.${name}_l)
endforeach()
foreach(name iswalnum iswalpha iswblank iswcntrl iswctype iswdigit iswgraph
             iswlower iswprint iswpunct iswspace iswupper iswxdigit
             towctrans towlower towupper wctrans wctype)
  list(APPEND character_api_deps libc.src.wctype.${name} libc.src.wctype.${name}_l)
endforeach()
add_libc_test(
  character_api_test
  SUITE libc-locale-tests
  SRCS character_api_test.cpp
  DEPENDS ${character_api_deps}
)

add_libc_test(
  collation_api_test
  SUITE libc-locale-tests
  SRCS collation_api_test.cpp
  DEPENDS libc.src.locale.locale_storage libc.src.locale.uselocale
    libc.src.string.strcoll libc.src.string.strcoll_l
    libc.src.string.strxfrm libc.src.string.strxfrm_l
    libc.src.wchar.wcscoll libc.src.wchar.wcscoll_l
    libc.src.wchar.wcsxfrm libc.src.wchar.wcsxfrm_l
    libc.src.__support.libc_errno libc.hdr.errno_macros libc.hdr.wchar_macros
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.locale.collation_api_test.__build__)
  target_compile_definitions(libc.test.src.locale.collation_api_test.__build__ PRIVATE
    COLLATION_ALLOCATION_TEST)
  target_link_options(libc.test.src.locale.collation_api_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=free)
endif()

add_libc_test(
  getlocalename_l_test
  SUITE libc-locale-tests
  SRCS getlocalename_l_test.cpp
  DEPENDS libc.src.locale.getlocalename_l libc.src.locale.newlocale
    libc.src.locale.duplocale libc.src.locale.freelocale libc.src.locale.setlocale
    libc.src.locale.uselocale libc.src.__support.libc_errno libc.config.app_h
    libc.src.__support.CPP.string
)

set(locale_file_test_deps)
set(locale_file_test_options)
if(LIBC_TARGET_OS_IS_LINUX)
  list(APPEND locale_file_test_deps libc.src.locale.linux_locale_file
    libc.src.stdlib.mkdtemp libc.hdr.fcntl_macros libc.hdr.sys_stat_macros
    libc.src.__support.OSUtil.osutil libc.include.sys_syscall)
  list(APPEND locale_file_test_options -DLOCALE_FILE_LINUX_TEST)
endif()
add_libc_test(
  locale_file_test
  SUITE libc-locale-tests
  SRCS locale_file_test.cpp
  DEPENDS libc.src.locale.locale_file ${locale_file_test_deps}
  COMPILE_OPTIONS ${locale_file_test_options}
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.locale.locale_file_test.__build__)
  target_compile_definitions(libc.test.src.locale.locale_file_test.__build__ PRIVATE
    LOCALE_FILE_ALLOCATION_TEST)
  target_link_options(libc.test.src.locale.locale_file_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=free)
endif()

add_libc_test(
  locale_ownership_test
  SUITE libc-locale-tests
  SRCS locale_ownership_test.cpp
  DEPENDS libc.src.locale.locale_storage libc.src.locale.newlocale
    libc.src.locale.duplocale libc.src.locale.freelocale libc.src.locale.uselocale
    libc.src.__support.libc_errno libc.hdr.errno_macros libc.config.app_h
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.locale.locale_ownership_test.__build__)
  target_compile_definitions(libc.test.src.locale.locale_ownership_test.__build__ PRIVATE
    LOCALE_OWNERSHIP_ALLOCATION_TEST)
  target_link_options(libc.test.src.locale.locale_ownership_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=free)
endif()

add_libc_test(
  collation_data_test
  SUITE libc-locale-tests
  SRCS collation_data_test.cpp
  DEPENDS libc.src.locale.collation_data libc.src.locale.collation_key
    libc.hdr.errno_macros
)

add_libc_test(
  transliteration_data_test
  SUITE libc-locale-tests
  SRCS transliteration_data_test.cpp
  DEPENDS libc.src.locale.transliteration_data
    libc.src.__support.wchar.encoding_converter libc.hdr.errno_macros
)

add_libc_test(
  encoding_data_test
  SUITE libc-locale-tests
  SRCS encoding_data_test.cpp
  DEPENDS libc.src.locale.encoding_data
)

set(regional_messages_test_deps)
set(regional_messages_test_options)
if(TARGET libc.src.locale.regional)
  list(APPEND regional_messages_test_deps libc.src.locale.regional)
  list(APPEND regional_messages_test_options -DREGIONAL_MESSAGES_TEST)
endif()
add_libc_test(
  messages_data_test
  SUITE libc-locale-tests
  SRCS messages_data_test.cpp
  DEPENDS libc.src.locale.messages_data libc.hdr.langinfo_macros ${regional_messages_test_deps}
  COMPILE_OPTIONS ${regional_messages_test_options}
)

set(time_category_test_deps)
if(LLVM_LIBC_FULL_BUILD)
  list(APPEND time_category_test_deps libc.src.locale.time_category
    libc.src.langinfo.nl_langinfo libc.src.langinfo.nl_langinfo_l
    libc.src.locale.uselocale)
endif()
add_libc_test(
  time_data_test
  SUITE libc-locale-tests
  SRCS time_data_test.cpp
  DEPENDS libc.src.locale.time_data libc.hdr.langinfo_macros ${time_category_test_deps}
)

add_libc_test(
  monetary_data_test
  SUITE libc-locale-tests
  SRCS monetary_data_test.cpp
  DEPENDS libc.src.locale.monetary_data
)

add_libc_test(
  ctype_data_test
  SUITE libc-locale-tests
  SRCS ctype_data_test.cpp
  DEPENDS libc.src.locale.ctype_data
)

add_libc_test(
  numeric_data_test
  SUITE libc-locale-tests
  SRCS numeric_data_test.cpp
  DEPENDS libc.src.locale.numeric_data libc.hdr.errno_macros
)

add_libc_test(
  locale_data_test
  SUITE libc-locale-tests
  SRCS locale_data_test.cpp
  DEPENDS libc.src.locale.locale_data libc.hdr.errno_macros
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.locale.locale_data_test.__build__)
  target_compile_definitions(libc.test.src.locale.locale_data_test.__build__ PRIVATE
    LOCALE_DATA_ALLOCATION_TEST)
  target_link_options(libc.test.src.locale.locale_data_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=free)
endif()

add_libc_test(
  locale_no_environment_test
  SUITE libc-locale-tests
  SRCS
    locale_no_environment_test.cpp
    ${LIBC_SOURCE_DIR}/src/locale/locale_name.cpp
  COMPILE_OPTIONS -DLIBC_TARGET_OS_IS_BAREMETAL
  DEPENDS
    libc.src.locale.locale_storage
    libc.hdr.errno_macros
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
