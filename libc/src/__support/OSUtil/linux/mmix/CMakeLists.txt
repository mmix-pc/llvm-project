add_object_library(
  dynamic_tls
  SRCS dynamic_tls.cpp dynamic_tls_fatal.cpp
  HDRS dynamic_tls.h
  DEPENDS .syscall libc.src.__support.OSUtil.linux.loader_services
    libc.include.sys_syscall
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fPIC -fvisibility=hidden
    -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector
    -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-lto
    -fno-sanitize=all -fno-profile-instr-generate -fno-profile-generate
    -fno-profile-arcs -fno-test-coverage -fno-coverage-mapping
    -Werror=global-constructors -Werror=exit-time-destructors
)

add_object_library(
  syscall_asm
  SRCS
    syscall.S
  HDRS
    syscall.h
)

# Keep the syscall interface available to independent consumers.
add_header_library(
  syscall
  HDRS
    syscall.h
  DEPENDS
    .syscall_asm
    libc.src.__support.common
)

add_header_library(
  linux_mmix_util
  HDRS
    syscall.h
  DEPENDS
    .syscall
)

add_object_library(
  heap
  SRCS
    heap.cpp
  HDRS
    heap.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.stdint_proxy
    libc.hdr.sys_mman_macros
    libc.src.__support.CPP.cstddef
    libc.src.__support.CPP.limits
    libc.src.__support.CPP.span
    libc.src.__support.OSUtil.linux.syscall_wrappers.mmap
    libc.src.__support.OSUtil.linux.syscall_wrappers.munmap
    libc.src.__support.block
    libc.src.__support.error_or
    libc.src.__support.freestore
)
