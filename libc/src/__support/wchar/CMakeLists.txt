add_header_library(
  mbstate
  HDRS
    mbstate.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.hdr.types.char32_t
)

add_object_library(
  encoding_descriptor
  SRCS encoding_descriptor.cpp
  HDRS encoding_descriptor.h
  DEPENDS .encoding_converter libc.src.__support.CPP.optional
    libc.src.__support.alloc_checker libc.hdr.errno_macros libc.hdr.func.free
)

add_object_library(
  scalar_codec
  SRCS scalar_codec.cpp
  HDRS scalar_codec.h
  DEPENDS
    .character_converter
    libc.hdr.errno_macros
    libc.hdr.stdint_proxy
    libc.src.__support.CPP.span
    libc.src.__support.error_or
)

add_header_library(
  string_converter
  HDRS
    string_converter.h
  DEPENDS
    libc.hdr.types.char8_t
    libc.hdr.types.char32_t
    libc.hdr.types.size_t
    libc.src.__support.common
    libc.src.__support.CPP.type_traits
    libc.src.__support.error_or
    .mbstate
    .character_converter
)

add_header_library(
  character_converter
  HDRS
    character_converter.h
  DEPENDS
    libc.src.locale.encoding
    libc.hdr.errno_macros
    libc.hdr.types.char8_t
    libc.hdr.types.char32_t
    libc.hdr.types.size_t
    libc.src.__support.common
    libc.src.__support.CPP.type_traits
    libc.src.__support.error_or
    libc.src.__support.macros.config
    libc.src.__support.macros.properties.types
    libc.src.__support.math_extras
    .mbstate
)

add_header_library(
  wcrtomb
  HDRS
    wcrtomb.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.char32_t
    libc.hdr.types.size_t
    libc.hdr.types.wchar_t
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.__support.libc_assert
    libc.src.__support.macros.null_check
    .restartable_converter
    .mbstate
)

add_object_library(
  mbrtowc
  HDRS
    mbrtowc.h
  SRCS
    mbrtowc.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.wchar_t
    libc.hdr.types.size_t
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.__support.macros.config
    .restartable_converter
    .mbstate
)

add_header_library(
  mbsnrtowcs
  HDRS
    mbsnrtowcs.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.wchar_t
    libc.hdr.types.size_t
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.__support.macros.config
    libc.src.__support.macros.null_check
    .restartable_string
    .mbstate
    .string_converter
)

add_header_library(
  wcsnrtombs
  HDRS
    wcsnrtombs.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.char8_t
    libc.hdr.types.char32_t
    libc.hdr.types.size_t
    libc.hdr.types.wchar_t
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.__support.macros.config
    libc.src.__support.macros.null_check
    .restartable_string
    .character_converter
    .mbstate
)
add_object_library(
  iso2022_cn
  SRCS iso2022_cn.cpp
  HDRS iso2022_cn.h
  DEPENDS .mbstate libc.src.locale.encoding_data libc.hdr.errno_macros
)

add_object_library(
  encoding_converter
  SRCS encoding_converter.cpp
  HDRS encoding_converter.h
  DEPENDS .iso2022_cn .scalar_codec libc.src.locale.transliteration_data
    libc.hdr.errno_macros libc.src.__support.CPP.string_view
)

add_object_library(
  restartable_converter
  SRCS restartable_converter.cpp
  HDRS restartable_converter.h
  DEPENDS .iso2022_cn .scalar_codec .character_converter .mbstate
    libc.src.locale.encoding
)

add_object_library(
  restartable_string
  SRCS restartable_string.cpp
  HDRS mbsnrtowcs.h wcsnrtombs.h
  DEPENDS .restartable_converter libc.src.__support.macros.null_check
    libc.hdr.types.size_t libc.hdr.types.wchar_t libc.hdr.errno_macros
)
