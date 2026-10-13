add_subdirectory(mmix)

add_object_library(
  entry
  SRCS entry.cpp
  HDRS entry.h
  DEPENDS .elf .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  elf
  SRCS elf.cpp
  HDRS elf.h
  DEPENDS .mmix.target libc.hdr.elf_proxy libc.src.__support.CPP.span
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  io
  SRCS io.cpp
  HDRS io.h syscall.h
  DEPENDS
    .mmix.syscall
    libc.hdr.types.size_t
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  arena
  SRCS arena.cpp
  HDRS arena.h
  DEPENDS
    .mmix.syscall
    libc.hdr.types.size_t
    libc.hdr.errno_macros
    libc.hdr.sys_mman_macros
    libc.include.sys_syscall
    # Only placement new is used; do not include the allocating new.cpp provider.
    libc.hdr.func.free
    libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)
