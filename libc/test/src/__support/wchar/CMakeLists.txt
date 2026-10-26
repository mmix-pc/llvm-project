add_custom_target(libc-support-wchar-tests)

add_libc_test(
  encoding_descriptor_test
  SUITE libc-support-wchar-tests
  SRCS encoding_descriptor_test.cpp
  DEPENDS libc.src.__support.wchar.encoding_descriptor libc.hdr.errno_macros
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.__support.wchar.encoding_descriptor_test.__build__)
  target_compile_definitions(libc.test.src.__support.wchar.encoding_descriptor_test.__build__ PRIVATE
    ENCODING_DESCRIPTOR_ALLOCATION_TEST)
  target_link_options(libc.test.src.__support.wchar.encoding_descriptor_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=free)
endif()

add_libc_test(
  encoding_converter_test
  SUITE libc-support-wchar-tests
  SRCS encoding_converter_test.cpp
  DEPENDS libc.src.__support.wchar.encoding_converter libc.hdr.errno_macros
)

add_libc_test(
  scalar_codec_test
  SUITE libc-support-wchar-tests
  SRCS scalar_codec_test.cpp
  DEPENDS libc.src.__support.wchar.scalar_codec libc.hdr.errno_macros
)

add_libc_test(
  string_converter_test
  SUITE
    libc-support-tests
  SRCS
    string_converter_test.cpp
  DEPENDS
    libc.test.src.locale_guard
    libc.src.__support.macros.properties.types
    libc.src.__support.wchar.string_converter
    libc.src.__support.wchar.mbstate
    libc.src.__support.error_or
    libc.hdr.errno_macros
    libc.hdr.types.char32_t
)

add_libc_test(
  utf8_to_32_test
  SUITE
    libc-support-tests
  SRCS
    utf8_to_32_test.cpp
  DEPENDS
    libc.src.__support.error_or
    libc.src.__support.macros.properties.types
    libc.src.__support.wchar.character_converter
    libc.src.__support.wchar.mbstate
)

add_libc_test(
  utf32_to_8_test
  SUITE
    libc-support-tests
  SRCS
    utf32_to_8_test.cpp
  DEPENDS
    libc.src.__support.error_or
    libc.src.__support.macros.properties.types
    libc.src.__support.wchar.character_converter
    libc.src.__support.wchar.mbstate
)

add_libc_test(
  wcsnrtombs_test
  SUITE
    libc-support-tests
  SRCS
    wcsnrtombs_test.cpp
  DEPENDS
    libc.test.src.locale_guard
    libc.src.__support.wchar.string_converter
    libc.src.__support.wchar.character_converter
    libc.src.__support.wchar.mbstate
    libc.src.__support.error_or
    libc.src.__support.wchar.wcsnrtombs
    libc.hdr.errno_macros
    libc.hdr.types.char32_t
    libc.hdr.types.char8_t
)
set(iso2022_cn_public_deps)
if(LLVM_LIBC_FULL_BUILD)
  list(APPEND iso2022_cn_public_deps
    libc.src.uchar.c16rtomb libc.src.uchar.mbrtoc16
    libc.src.uchar.c32rtomb libc.src.uchar.mbrtoc32
    libc.src.wchar.mbrtowc libc.src.wchar.mbrlen libc.src.wchar.wcrtomb
    libc.src.wchar.mbsrtowcs libc.src.wchar.mbsnrtowcs
    libc.src.wchar.wcsrtombs libc.src.wchar.wcsnrtombs
    libc.src.stdlib.mblen libc.src.stdlib.mbtowc libc.src.stdlib.wctomb
    libc.src.stdlib.mbstowcs libc.src.stdlib.wcstombs
    libc.src.wchar.btowc libc.src.wchar.wctob libc.src.locale.__ctype_get_mb_cur_max
    libc.src.wchar.mbsinit libc.src.locale.locale libc.src.__support.libc_errno)
endif()
set(iso2022_cn_file_option)
if(LLVM_LIBC_FULL_BUILD AND TARGET libc.src.__support.File.file)
  list(APPEND iso2022_cn_public_deps libc.src.__support.File.file
    libc.src.locale.locale_storage libc.src.stdio.snprintf
    libc.src.wchar.swprintf libc.src.wchar.swscanf)
  set(iso2022_cn_file_option -DLIBC_TEST_HAS_FILE)
endif()
add_libc_test(
  iso2022_cn_test
  SUITE libc-support-tests
  SRCS iso2022_cn_test.cpp
  COMPILE_OPTIONS ${iso2022_cn_file_option}
  DEPENDS libc.src.__support.wchar.iso2022_cn
    libc.src.__support.wchar.restartable_converter
    libc.src.fnmatch.matcher
    ${iso2022_cn_public_deps}
)
