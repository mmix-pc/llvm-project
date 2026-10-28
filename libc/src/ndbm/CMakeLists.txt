add_object_library(
  hash_validation
  SRCS hash_validation.cpp
  HDRS hash_validation.h
  DEPENDS libc.hdr.types.size_t libc.hdr.stdint_proxy
    libc.src.__support.macros.config
)

add_object_library(
  hash_page
  SRCS hash_page.cpp
  HDRS hash_page.h
  DEPENDS .hash_validation
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.memory_utils.inline_memmove
)

add_object_library(
  hash_io
  SRCS hash_io.cpp
  HDRS hash_io.h
  DEPENDS .hash_validation
    libc.hdr.errno_macros
    libc.src.__support.error_or
    libc.src.string.memory_utils.inline_memcpy
)

add_object_library(
  hash_function
  SRCS hash_function.cpp
  HDRS hash_function.h
  DEPENDS .hash_validation
)

add_object_library(
  hash_reader
  SRCS hash_reader.cpp
  HDRS hash_reader.h
  DEPENDS .hash_function .hash_io .hash_page
    libc.hdr.errno_macros
)

add_object_library(
  hash_growth
  SRCS hash_growth.cpp
  HDRS hash_growth.h
  DEPENDS .hash_function .hash_validation
    libc.hdr.errno_macros libc.src.__support.error_or
)

add_object_library(
  hash_database
  SRCS hash_database.cpp
  HDRS hash_database.h
  DEPENDS .hash_reader libc.hdr.errno_macros
    libc.src.__support.alloc_checker libc.src.__support.CPP.new
    libc.src.stdlib.qsort_util
    libc.src.string.memory_utils.inline_memcpy
)

add_object_library(
  hash_overflow
  SRCS hash_overflow.cpp
  HDRS hash_overflow.h
  DEPENDS .hash_function .hash_io libc.hdr.errno_macros
)
