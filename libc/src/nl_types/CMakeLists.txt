add_object_library(
  catalog_data
  SRCS catalog_data.cpp
  HDRS catalog_data.h
  DEPENDS libc.hdr.stdint_proxy libc.hdr.errno_macros libc.hdr.func.free
    libc.src.__support.CPP.span libc.src.__support.error_or
    libc.src.__support.alloc_checker libc.src.stdlib.qsort_util
)

add_entrypoint_object(
  catopen
  SRCS
    catopen.cpp
  HDRS
    catopen.h
  DEPENDS
    libc.include.llvm-libc-types.nl_catd
    libc.src.errno.errno
)

add_entrypoint_object(
  catclose
  SRCS
    catclose.cpp
  HDRS
    catclose.h
  DEPENDS
    libc.include.llvm-libc-types.nl_catd
)

add_entrypoint_object(
  catgets
  SRCS
    catgets.cpp
  HDRS
    catgets.h
  DEPENDS
    libc.include.llvm-libc-types.nl_catd
)
