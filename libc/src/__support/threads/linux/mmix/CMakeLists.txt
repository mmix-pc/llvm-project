add_header_library(
  tls_h
  HDRS tls.h
  DEPENDS libc.config.app_h
)

add_object_library(
  tls
  SRCS tls.cpp
  DEPENDS
    .tls_h
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.include.sys_syscall
  COMPILE_OPTIONS -ffreestanding -fno-builtin -fno-stack-protector -fno-lto
)

if(LIBC_MMIX_ENABLE_STATIC_TLS)
  include(${CMAKE_CURRENT_LIST_DIR}/../../../../../config/linux/mmix/RuntimeMode.cmake)
  mmix_check_runtime_mode()
  set(mmix_main_errno_dependency libc.src.errno.errno)
  set(mmix_main_lifecycle_dependency .lifecycle)
  # Internal primitives only; worker creation and public pthread remain unselected.
  add_object_library(
    lifecycle
    SRCS lifecycle.cpp
    HDRS lifecycle.h
    DEPENDS
      .tls_h
      libc.src.__support.threads.raw_mutex
      libc.src.__support.threads.thread_attributes
  )
  add_object_library(
    resources
    SRCS resources.cpp
    HDRS resources.h
    DEPENDS
      .lifecycle
      .tls
      libc.hdr.pthread_macros
      libc.src.__support.CPP.new
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
  )
  add_object_library(
    thread_entry
    SRCS thread_entry.S
    HDRS thread_entry.h
    DEPENDS .tls_h libc.include.sys_syscall
  )
  # Remains unselected until creator and terminal lifecycle providers exist.
  add_object_library(
    thread_start
    SRCS thread_start.cpp
    HDRS thread_start.h
    DEPENDS
      .thread_entry
      .lifecycle
      libc.src.__support.threads.thread_common
      libc.src.errno.errno
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto -fno-optimize-sibling-calls
  )
  add_object_library(
    thread_create
    SRCS thread_create.cpp
    HDRS thread_create.h
    DEPENDS
      .resources
      .thread_entry
      libc.hdr.signal_macros
      libc.hdr.time_macros
      libc.hdr.types.struct_timespec
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto
  )
endif()

add_object_library(
  main_thread
  SRCS
    main_thread.cpp
  HDRS
    main_thread.h
  DEPENDS
    libc.src.__support.threads.thread_common
    ${mmix_main_errno_dependency}
    ${mmix_main_lifecycle_dependency}
    libc.src.__support.CPP.limits
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.include.sys_syscall
)
