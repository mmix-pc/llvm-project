add_object_library(
  mo_catalog
  SRCS mo_catalog.cpp
  HDRS mo_catalog.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.hdr.errno_macros
    libc.hdr.func.free
    libc.src.__support.alloc_checker
    libc.src.__support.CPP.atomic
    libc.src.__support.CPP.span
    libc.src.__support.CPP.string_view
    libc.src.__support.error_or
    libc.src.__support.macros.config
)
