# Bootstrap providers remain native and unprotected; runtime code is protected.
if(LIBC_MMIX_ENABLE_STATIC_TLS)
  include(${CMAKE_CURRENT_LIST_DIR}/../../../config/linux/mmix/RuntimeMode.cmake)
  mmix_check_runtime_mode()
  add_startup_object(
    tls_prepare
    SRC tls_prepare.cpp
    DEPENDS
      .process_args
      libc.src.__support.threads.linux.mmix.tls
      libc.src.__support.threads.linux.mmix.main_thread
    COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
  )
  add_startup_object(
    tls_post
    SRC tls_post.cpp
    DEPENDS .process_args libc.src.__support.threads.linux.mmix.main_thread
    COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
  )
  set(mmix_tls_startup .tls_prepare .tls_handoff .tls_post)
endif()
add_header_library(
  tls_handoff_h
  HDRS tls_handoff.h
  DEPENDS libc.src.__support.macros.config
)

add_startup_object(
  tls_handoff
  SRC tls_handoff.cpp
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
)

add_startup_object(
  start
  SRC start.cpp
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
)

add_startup_object(
  process_args
  SRC process_args.cpp
  DEPENDS
    libc.config.app_h
    libc.hdr.elf_proxy
    libc.src.__support.threads.linux.mmix.tls_h
    libc.src.__support.OSUtil.linux.auxv
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
)

# Environment/name storage is supplied by the existing libc providers when
# the complete CRT is composed; preparation does not export new entrypoints.
add_startup_object(
  bootstrap
  SRC bootstrap.cpp
  DEPENDS
    .process_args
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
)

add_startup_object(
  stack_guard
  SRC stack_guard.cpp
  DEPENDS
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.include.sys_syscall
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
)

# Guard initialization precedes this ordinary protected runtime path.
if(LIBC_MMIX_BUILD_UNWIND_ROOTS)
  set(mmix_root_options -DLIBC_MMIX_UNWIND_ROOTS -funwind-tables)
endif()
add_startup_object(
  lifecycle
  SRC lifecycle.cpp
  DEPENDS
    libc.config.app_h
    libc.hdr.types.size_t
    libc.src.__support.threads.linux.mmix.main_thread
  COMPILE_OPTIONS -ffreestanding -fno-builtin ${mmix_root_options}
)

# Keep libc state, termination and syscall providers in the runtime archive.
merge_relocatable_object(
  crt1
  .start
  .process_args
  .bootstrap
  .stack_guard
  .lifecycle
  ${mmix_tls_startup}
)

add_custom_target(libc-startup)
add_dependencies(libc-startup libc.startup.linux.mmix.crt1)
