add_object_library(
  debug
  SRCS debug.cpp
  DEPENDS libc.hdr.types.struct_r_debug libc.hdr.elf_proxy
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  tls_pointer
  SRCS tls_pointer.cpp
  DEPENDS libc.loader.linux.tls_allocation
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  continuation
  SRCS continuation.cpp
  DEPENDS libc.loader.linux.startup libc.loader.linux.io
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  bootstrap
  SRCS bootstrap.cpp
  DEPENDS
    libc.loader.linux.entry
    libc.loader.linux.elf
    libc.loader.linux.arena
    libc.loader.linux.io
    libc.loader.linux.self_relocation
    libc.loader.linux.protection
    .syscall
    libc.hdr.sys_mman_macros
    libc.include.sys_syscall
    libc.hdr.func.free
    libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

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
    libc.src.__support.OSUtil.linux.mmix.syscall_asm
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)
