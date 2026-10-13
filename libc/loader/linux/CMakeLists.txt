add_subdirectory(mmix)

add_object_library(
  search
  SRCS search.cpp
  HDRS search.h
  DEPENDS .mapping libc.hdr.errno_macros
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  mapping
  SRCS mapping.cpp
  HDRS mapping.h
  DEPENDS .dynamic .elf .arena .io .mmix.target .mmix.syscall
    libc.hdr.errno_macros libc.hdr.stdio_macros libc.hdr.sys_mman_macros
    libc.include.sys_syscall libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  dynamic
  SRCS dynamic.cpp
  HDRS dynamic.h
  DEPENDS .elf .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  self_relocation
  SRCS self_relocation.cpp
  HDRS self_relocation.h
  DEPENDS .elf .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

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
