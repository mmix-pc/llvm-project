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
    libc.src.__support.CPP.string_view
    libc.src.__support.CPP.span
    libc.src.__support.error_or
)

add_object_library(
  locale_file
  SRCS locale_file.cpp
  HDRS locale_file.h
  DEPENDS .locale_data libc.hdr.errno_macros libc.hdr.func.free
    libc.src.__support.alloc_checker libc.src.__support.CPP.string_view
)

if(LIBC_TARGET_OS_IS_LINUX)
  set(LIBC_LOCALE_DATA_ROOT "/usr/share/llvm-libc/locale" CACHE STRING
      "Target-system root for LLVM libc private locale data")
  if(NOT IS_ABSOLUTE "${LIBC_LOCALE_DATA_ROOT}")
    message(FATAL_ERROR "LIBC_LOCALE_DATA_ROOT must be a target absolute path")
  endif()
  add_object_library(
    linux_locale_file
    SRCS linux/locale_file.cpp
    DEPENDS .locale_file libc.hdr.fcntl_macros libc.hdr.sys_stat_macros
      libc.src.__support.OSUtil.linux.stat.kernel_statx_types
      libc.src.__support.OSUtil.linux.syscall_wrappers.close
      libc.src.__support.OSUtil.linux.syscall_wrappers.read
      libc.src.__support.OSUtil.linux.syscall_wrappers.statx
    COMPILE_OPTIONS "-DLIBC_LOCALE_DATA_ROOT=\"${LIBC_LOCALE_DATA_ROOT}\""
  )
endif()

add_object_library(
  collation_key
  SRCS collation_key.cpp
  HDRS collation_key.h
  DEPENDS .collation_data libc.src.__support.CPP.optional libc.hdr.errno_macros
    libc.hdr.types.wchar_t
)

add_object_library(
  collation_data
  SRCS collation_data.cpp
  HDRS collation_data.h
  DEPENDS .locale_data libc.hdr.errno_macros
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
  builtin_transliteration
  SRCS builtin_transliteration.cpp
  HDRS builtin_transliteration.h builtin_transliteration.inc
  DEPENDS .transliteration_data
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
    libc.src.__support.CPP.string_view libc.src.__support.CPP.limits
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
  convention_types
  HDRS numeric_data.h monetary_data.h time_data.h messages_data.h transliteration_data.h encoding_data.h
  DEPENDS .locale_data libc.src.__support.CPP.string_view
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

add_object_library(
  collation
  SRCS collation.cpp
  HDRS collation.h
  DEPENDS .collation_key ${encoding_deps}
    libc.src.__support.wchar.scalar_codec libc.src.__support.alloc_checker
    libc.src.__support.libc_errno libc.hdr.errno_macros libc.hdr.func.free
    libc.hdr.types.locale_t libc.hdr.types.size_t libc.hdr.types.wchar_t
)

add_object_library(
  character
  SRCS character.cpp
  HDRS character.h
  DEPENDS ${encoding_deps} libc.src.__support.wctype_impl
    libc.src.__support.CPP.limits libc.hdr.types.wint_t libc.hdr.wchar_macros
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
    libc.src.__support.CPP.optional
    libc.src.__support.CPP.string_view
    .collation_data
    .ctype_data
    .convention_types
)

add_object_library(
  locale_storage
  SRCS locale_storage.cpp
  HDRS locale_storage.h
  DEPENDS .locale .locale_data .encoding_data libc.src.__support.alloc_checker
    libc.hdr.errno_macros libc.hdr.func.free
)

add_entrypoint_object(
  getlocalename_l
  SRCS getlocalename_l.cpp
  HDRS getlocalename_l.h
  DEPENDS .locale_storage libc.hdr.types.locale_t
)

add_object_library(
  conventions
  SRCS conventions.cpp
  HDRS conventions.h
  DEPENDS .locale_storage .numeric_data .monetary_data
    libc.src.__support.alloc_checker libc.src.__support.CPP.limits
    libc.hdr.errno_macros
)

add_object_library(
  time_category
  SRCS time_category.cpp
  HDRS time_category.h
  DEPENDS .locale_storage .time_data libc.src.__support.alloc_checker
    libc.hdr.errno_macros
)

set(locale_environment_deps)
if(NOT LIBC_TARGET_OS_IS_BAREMETAL)
  list(APPEND locale_environment_deps libc.src.stdlib.environ_internal)
endif()

set(regional_locale_deps)
set(regional_locale_options)
if(LIBC_TARGET_OS_IS_LINUX AND
   "libc.src.regex.regcomp" IN_LIST TARGET_LIBC_ENTRYPOINTS AND
   "libc.src.regex.regfree" IN_LIST TARGET_LIBC_ENTRYPOINTS)
  add_object_library(
    regional
    SRCS regional.cpp
    HDRS regional.h
    DEPENDS .linux_locale_file .locale_storage .conventions .time_category
      .messages_data .transliteration_data libc.src.regex.regcomp libc.src.regex.regfree
      libc.hdr.regex_macros
  )
  list(APPEND regional_locale_deps .regional)
  list(APPEND regional_locale_options -DLIBC_LOCALE_HAS_REGIONAL_DATA)
endif()

add_object_library(
  locale_name
  SRCS
    locale_name.cpp
  DEPENDS
    .locale_storage
    libc.src.__support.CPP.string_view
    libc.hdr.errno_macros
    ${locale_environment_deps}
    ${regional_locale_deps}
  COMPILE_OPTIONS ${regional_locale_options}
)

add_entrypoint_object(
  localeconv
  SRCS
    localeconv.cpp
  HDRS
    localeconv.h
  DEPENDS
    libc.include.locale
    .locale
    libc.src.__support.CPP.limits
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
    .locale_storage
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
    .locale_storage
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
    .locale_storage
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
    .locale_storage
    libc.hdr.types.locale_t
    libc.hdr.func.free
)
