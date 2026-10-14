add_subdirectory(mmix)

add_object_library(
  locks
  SRCS locks.cpp
  HDRS locks.h
  DEPENDS .mmix.syscall libc.src.__support.CPP.atomic
    libc.src.__support.OSUtil.linux.loader_services
    libc.hdr.errno_macros libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  image_registry
  SRCS image_registry.cpp
  HDRS image_registry.h
  DEPENDS .arena .tls_layout libc.hdr.errno_macros
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime
  SRCS runtime.cpp
  HDRS runtime.h
  DEPENDS .graph .entry .tls_resolver .unwind_images libc.hdr.errno_macros
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  unwind_images
  SRCS unwind_images.cpp
  HDRS unwind_images.h
  DEPENDS .elf .arena .io libc.hdr.errno_macros
    libc.src.__support.OSUtil.linux.loader_services
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  tls_resolver
  SRCS tls_resolver.cpp
  HDRS tls_resolver.h
  DEPENDS .tls_allocation .io .mmix.tls_pointer
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  tls_allocation
  SRCS tls_allocation.cpp
  HDRS tls_allocation.h
  DEPENDS .relocation .mmix.syscall .mmix.target libc.hdr.errno_macros
    libc.hdr.sys_mman_macros libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  tls_layout
  SRCS tls_layout.cpp
  HDRS tls_layout.h
  DEPENDS .elf .mmix.target libc.src.__support.OSUtil.linux.loader_services
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  startup
  SRCS startup.cpp
  HDRS startup.h
  DEPENDS .initialization .entry .graph .io .arena .unwind_images .runtime .mmix.target .mmix.syscall
    libc.hdr.errno_macros libc.hdr.fcntl_macros libc.include.sys_syscall
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  initialization
  SRCS initialization.cpp
  HDRS initialization.h
  DEPENDS .graph .entry .arena .mmix.target libc.hdr.errno_macros
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  protection
  SRCS protection.cpp
  HDRS protection.h
  DEPENDS .elf .mmix.target .mmix.syscall libc.hdr.errno_macros
    libc.hdr.sys_mman_macros libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  relocation
  SRCS relocation.cpp
  HDRS relocation.h
  DEPENDS .symbols .dynamic .elf .self_relocation .mmix.target .tls_layout
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  symbols
  SRCS symbols.cpp
  HDRS symbols.h
  DEPENDS .dynamic .elf .arena .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  graph
  SRCS graph.cpp
  HDRS graph.h
  DEPENDS .search .mapping .io .arena .symbols .relocation .protection libc.hdr.errno_macros
    libc.include.sys_syscall libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

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
    libc.hdr.sys_stat_macros
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
