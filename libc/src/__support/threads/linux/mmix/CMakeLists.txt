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
    libc.src.__support.CPP.limits
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.include.sys_syscall
)
