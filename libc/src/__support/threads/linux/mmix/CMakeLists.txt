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
  # Internal primitives; only explicit lifecycle/public compositions select workers.
  add_object_library(
    lifecycle
    SRCS lifecycle.cpp
    HDRS lifecycle.h unwind_root.h
    DEPENDS
      .tls_h
      libc.src.__support.threads.raw_mutex
      libc.src.__support.threads.thread_attributes
  )
  if(LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM")
    add_object_library(
      process_operation
      SRCS process_operation.cpp
      HDRS process_operation.h
      DEPENDS
        .lifecycle
        libc.hdr.errno_macros
        libc.hdr.signal_macros
        libc.src.__support.OSUtil.linux.mmix.syscall
        libc.include.sys_syscall
    )
    set(mmix_process_operation_dependency .process_operation)
  endif()
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
  # Native child entry is selected only by explicit thread compositions.
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
    SRCS thread_create.cpp thread_reaper.cpp
    HDRS thread_create.h thread_reaper.h
    DEPENDS
      .thread_reaper_service
      .thread_reclaim
      .resources
      .thread_entry
      libc.src.__support.threads.thread_common
      libc.hdr.signal_macros
      libc.hdr.time_macros
      libc.hdr.types.struct_timespec
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto
  )
  add_object_library(
    thread_adapter
    SRCS thread_adapter.cpp
    DEPENDS
      .thread_create
      .thread_join
      .thread_finish
      libc.config.app_h
      libc.hdr.signal_macros
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.src.__support.threads.thread_common
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto
  )
  if(LIBC_MMIX_BUILD_PTHREAD_ONCE)
    add_object_library(
      once
      SRCS once.cpp
      HDRS once.h
      DEPENDS libc.src.__support.threads.callonce
    )
    set(mmix_once_dependency .once)
    set(mmix_once_option -DLIBC_MMIX_PUBLIC_ONCE)
  endif()
  if(LIBC_MMIX_BUILD_UNWIND_ROOTS)
    add_object_library(
      unwind_root
      SRCS unwind_root.cpp
      HDRS unwind_root.h
      DEPENDS .lifecycle libc.src.__support.OSUtil.linux.mmix.syscall
              libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -funwind-tables
        -D_LIBUNWIND_IS_NATIVE_ONLY
        -I${LIBC_SOURCE_DIR}/../libunwind/include
    )
    set(mmix_root_dependency .unwind_root)
    set(mmix_root_options -DLIBC_MMIX_UNWIND_ROOTS -funwind-tables)
  endif()
  if(LIBC_MMIX_BUILD_TLS_DESTRUCTORS)
    add_object_library(
      tls_destructors
      SRCS tls_destructors.cpp
      HDRS tls_destructors.h
      DEPENDS .lifecycle libc.src.__support.threads.thread_common
              libc.src.__support.CPP.new libc.src.stdlib.malloc libc.src.stdlib.free
      COMPILE_OPTIONS -fno-lto
    )
  endif()
  add_object_library(
    thread_finish
    SRCS thread_finish.cpp thread_process_exit.cpp
    HDRS thread_finish.h
    DEPENDS
      ${mmix_once_dependency}
      ${mmix_root_dependency}
      .thread_start
      .main_thread
      .lifecycle
      libc.src.__support.OSUtil.osutil
      libc.src.stdlib.exit
      libc.hdr.signal_macros
      libc.src.__support.threads.thread_common
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto -fno-optimize-sibling-calls ${mmix_once_option}
                    ${mmix_root_options}
  )
  add_object_library(
    thread_reclaim
    SRCS thread_reclaim.cpp
    HDRS thread_reclaim.h
    DEPENDS
      .resources
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto
  )
  add_object_library(
    thread_join
    SRCS thread_join.cpp
    HDRS thread_join.h
    DEPENDS
      .thread_reclaim
      libc.hdr.errno_macros
      libc.src.__support.threads.thread_common
    COMPILE_OPTIONS -fno-lto
  )
  add_object_library(
    thread_detach
    SRCS thread_detach.cpp
    HDRS thread_detach.h
    DEPENDS .lifecycle libc.hdr.errno_macros
  )
  add_object_library(
    thread_signal
    SRCS thread_signal.cpp
    HDRS thread_signal.h
    DEPENDS
      .lifecycle
      libc.hdr.errno_macros
      libc.hdr.signal_macros
      libc.src.__support.threads.thread_common
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto
  )
  add_object_library(
    thread_reaper_service
    SRCS thread_reaper_loop.cpp
    DEPENDS
      .resources
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto
  )
endif()

add_header_library(
  process_operation_h
  HDRS process_operation.h
  DEPENDS libc.src.__support.macros.attributes ${mmix_process_operation_dependency}
)

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
