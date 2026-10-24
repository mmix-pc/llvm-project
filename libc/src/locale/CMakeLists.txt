add_object_library(
  locale_data
  SRCS locale_data.cpp
  HDRS locale_data.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.stdint_proxy
    libc.hdr.func.free
    libc.src.__support.alloc_checker
    libc.src.__support.CPP.atomic
    libc.src.__support.CPP.span
    libc.src.__support.error_or
)

add_object_library(
  ctype_data
  SRCS ctype_data.cpp
  HDRS ctype_data.h
  DEPENDS .locale_data libc.hdr.errno_macros
)

add_object_library(
  encoding_data
  SRCS encoding_data.cpp
  HDRS encoding_data.h
  DEPENDS .locale_data libc.hdr.errno_macros
)

add_object_library(
  transliteration_data
  SRCS transliteration_data.cpp
  HDRS transliteration_data.h
  DEPENDS .locale_data libc.hdr.errno_macros
)

add_object_library(
  messages_data
  SRCS messages_data.cpp
  HDRS messages_data.h
  DEPENDS .locale_data .category_validation libc.hdr.errno_macros
)

add_object_library(
  time_data
  SRCS time_data.cpp
  HDRS time_data.h
  DEPENDS .locale_data .category_validation libc.hdr.errno_macros
)

add_object_library(
  numeric_data
  SRCS numeric_data.cpp
  HDRS numeric_data.h
  DEPENDS .locale_data .category_validation libc.hdr.errno_macros
)

add_object_library(
  monetary_data
  SRCS monetary_data.cpp
  HDRS monetary_data.h
  DEPENDS .locale_data .category_validation libc.hdr.errno_macros
)

add_header_library(
  category_validation
  HDRS category_validation.h
  DEPENDS .locale_data libc.src.__support.wchar.character_converter
)

set(encoding_deps)
if(LLVM_LIBC_FULL_BUILD)
  list(APPEND encoding_deps .locale)
endif()
add_header_library(
  encoding
  HDRS encoding.h
  DEPENDS ${encoding_deps} libc.src.__support.macros.config
)

if(NOT LLVM_LIBC_FULL_BUILD)
  return()
endif()

add_entrypoint_object(
  __ctype_get_mb_cur_max
  SRCS mb_cur_max.cpp
  HDRS mb_cur_max.h
  DEPENDS .encoding libc.hdr.types.size_t
)

add_object_library(
  locale
  SRCS
    locale.cpp
  HDRS
    locale.h
  DEPENDS
    libc.hdr.types.locale_t
    libc.hdr.locale_macros
    libc.src.__support.CPP.atomic
)

set(locale_environment_deps)
if(NOT LIBC_TARGET_OS_IS_BAREMETAL)
  list(APPEND locale_environment_deps libc.src.stdlib.environ_internal)
endif()

add_object_library(
  locale_name
  SRCS
    locale_name.cpp
  DEPENDS
    .locale
    libc.src.__support.CPP.string_view
    ${locale_environment_deps}
)

add_entrypoint_object(
  localeconv
  SRCS
    localeconv.cpp
  HDRS
    localeconv.h
  DEPENDS
    libc.include.locale
  CXX_STANDARD
    20 # For designated initializers
)

add_entrypoint_object(
  newlocale
  SRCS
    newlocale.cpp
  HDRS
    newlocale.h
  DEPENDS
    libc.hdr.locale_macros
    libc.hdr.types.locale_t
    .locale
    .locale_name
    libc.hdr.errno_macros
    libc.hdr.func.malloc
    libc.src.__support.libc_errno
)

add_entrypoint_object(
  duplocale
  SRCS
    duplocale.cpp
  HDRS
    duplocale.h
  DEPENDS
    libc.hdr.types.locale_t
    .locale
    libc.hdr.errno_macros
    libc.hdr.func.malloc
    libc.src.__support.libc_errno
)

add_entrypoint_object(
  setlocale
  SRCS
    setlocale.cpp
  HDRS
    setlocale.h
  DEPENDS
    libc.hdr.locale_macros
    libc.hdr.types.locale_t
    .locale_name
    libc.hdr.errno_macros
    libc.src.__support.libc_errno
)

add_entrypoint_object(
  uselocale
  SRCS
    uselocale.cpp
  HDRS
    uselocale.h
  DEPENDS
    libc.hdr.types.locale_t
    .locale
)

add_entrypoint_object(
  freelocale
  SRCS
    freelocale.cpp
  HDRS
    freelocale.h
  DEPENDS
    libc.hdr.types.locale_t
    libc.hdr.func.free
)
