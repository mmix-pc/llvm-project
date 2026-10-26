add_object_library(
  pattern
  SRCS pattern.cpp
  HDRS pattern.h
  DEPENDS .parameter libc.src.fnmatch.matcher libc.src.__support.char_vector
    libc.src.__support.alloc_checker libc.src.__support.CPP.limits
    libc.hdr.errno_macros libc.hdr.func.free
)

add_object_library(
  dollar_quote
  SRCS dollar_quote.cpp
  HDRS dollar_quote.h
  DEPENDS libc.hdr.stdint_proxy libc.src.__support.CPP.span
)

add_entrypoint_object(
  wordfree
  SRCS wordfree.cpp
  HDRS wordfree.h
  DEPENDS .results libc.hdr.types.wordexp_t libc.src.__support.common
    libc.src.__support.libc_errno
)

add_object_library(
  results
  SRCS results.cpp
  HDRS results.h
  DEPENDS libc.hdr.types.wordexp_t libc.hdr.errno_macros
    libc.hdr.func.free libc.hdr.func.malloc libc.hdr.func.realloc
    libc.src.__support.CPP.limits libc.src.__support.CPP.string_view
    libc.src.string.memory_utils.inline_memcpy
)

add_object_library(
  syntax
  SRCS syntax.cpp
  HDRS syntax.h
  DEPENDS .dollar_quote libc.hdr.stdint_proxy libc.src.__support.CPP.span
)

add_object_library(
  parameter
  SRCS parameter.cpp
  HDRS parameter.h
  DEPENDS libc.hdr.errno_macros libc.src.__support.CPP.string_view
    libc.src.__support.error_or
)

add_object_library(
  fields
  SRCS fields.cpp
  HDRS fields.h
  DEPENDS libc.hdr.errno_macros libc.src.__support.CPP.limits
    libc.src.__support.CPP.span libc.src.__support.CPP.string_view
    libc.src.__support.error_or
)

add_object_library(
  arithmetic
  SRCS arithmetic.cpp
  HDRS arithmetic.h
  DEPENDS libc.hdr.errno_macros libc.src.__support.CPP.limits
    libc.src.__support.CPP.string_view libc.src.__support.error_or
)
