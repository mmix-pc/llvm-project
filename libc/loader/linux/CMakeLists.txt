add_subdirectory(mmix)

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
