add_object_library(
  target
  SRCS target.cpp
  HDRS ../target.h
  DEPENDS libc.hdr.elf_proxy libc.src.__support.macros.config
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)
