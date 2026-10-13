add_object_library(
  entry
  SRCS entry.S
)

add_object_library(
  target
  SRCS target.cpp
  HDRS ../target.h
  DEPENDS libc.hdr.elf_proxy libc.src.__support.macros.config
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  syscall
  SRCS syscall.cpp
  HDRS ../syscall.h
  DEPENDS libc.src.__support.OSUtil.linux.mmix.syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)
