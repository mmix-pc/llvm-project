add_object_library(
  matcher
  SRCS matcher.cpp
  HDRS matcher.h
  DEPENDS libc.src.__support.wchar.restartable_converter
    libc.src.locale.character libc.src.locale.collation_key
    libc.src.__support.alloc_checker libc.src.__support.CPP.limits
    libc.hdr.fnmatch_macros libc.hdr.func.free libc.hdr.errno_macros
)

add_entrypoint_object(
  fnmatch
  SRCS
    fnmatch.cpp
  HDRS
    fnmatch.h
  DEPENDS
    libc.hdr.fnmatch_macros
    libc.src.__support.common
    .matcher
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.__support.wctype_impl
)
