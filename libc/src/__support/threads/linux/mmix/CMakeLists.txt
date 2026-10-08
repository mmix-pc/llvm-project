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
  if(LIBC_MMIX_BUILD_PROTECT_MUTEX OR LIBC_MMIX_BUILD_PTHREAD_SCHEDULING)
    if(LIBC_MMIX_BUILD_PTHREAD_SCHEDULING)
      set(mmix_scheduling_options -DLIBC_MMIX_PTHREAD_SCHEDULING)
    endif()
    add_object_library(
      priority_protection
      SRCS priority_protection.cpp priority_kernel.cpp
      HDRS priority_protection_state.h
      DEPENDS .lifecycle .robust_mutex libc.src.__support.OSUtil.osutil
        libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -ftls-model=local-exec ${mmix_scheduling_options}
    )
  endif()
  if(LIBC_MMIX_BUILD_PROTECT_MUTEX)
    add_object_library(
      protect_mutex
      SRCS protect_mutex.cpp
      HDRS protect_mutex.h priority_protection_state.h mutex_flags.h
      DEPENDS .priority_protection .lifecycle .robust_mutex libc.src.__support.OSUtil.osutil
        libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -ftls-model=local-exec
    )
  endif()
  if(LIBC_MMIX_BUILD_ROBUST_MUTEX)
    if(LIBC_MMIX_BUILD_PI_MUTEX)
      set(mmix_pi_sources pi_mutex.cpp)
      set(mmix_pi_options -DLIBC_MMIX_PI_MUTEX)
    endif()
    add_object_library(
      robust_mutex
      SRCS robust_mutex.cpp ${mmix_pi_sources}
      HDRS robust_mutex.h
      DEPENDS libc.src.__support.threads.raw_mutex
        libc.src.__support.OSUtil.osutil libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -ftls-model=local-exec ${mmix_pi_options}
    )
    set(mmix_robust_dependency .robust_mutex)
    set(mmix_robust_options -DLIBC_MMIX_ROBUST_MUTEX)
  endif()
  set(mmix_main_errno_dependency libc.src.errno.errno)
  if(LIBC_MMIX_BUILD_PTHREAD_SCHEDULING)
    add_object_library(
      thread_scheduling
      SRCS thread_scheduling.cpp
      HDRS thread_scheduling.h
      DEPENDS .priority_protection .lifecycle libc.hdr.signal_macros
        libc.src.__support.OSUtil.linux.mmix.syscall libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto
    )
    set(mmix_scheduling_dependency .thread_scheduling)
  endif()
  set(mmix_main_lifecycle_dependency .lifecycle)
  # Internal primitives; only explicit lifecycle/public compositions select workers.
  add_object_library(
    lifecycle
    SRCS lifecycle.cpp
    HDRS lifecycle.h unwind_root.h cancellation_state.h
    DEPENDS
      .tls_h
      libc.src.__support.threads.raw_mutex
      libc.src.__support.threads.thread_attributes
      libc.hdr.errno_macros
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
      ${mmix_robust_dependency}
      ${mmix_scheduling_dependency}
      .thread_entry
      .lifecycle
      libc.src.__support.threads.thread_common
      libc.src.errno.errno
      libc.src.__support.CPP.limits
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto -fno-optimize-sibling-calls ${mmix_robust_options}
      ${mmix_scheduling_options}
  )
  add_object_library(
    thread_create
    SRCS thread_create.cpp thread_reaper.cpp
    HDRS thread_create.h thread_reaper.h
    DEPENDS
      .thread_reaper_service
      ${mmix_scheduling_dependency}
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
    COMPILE_OPTIONS -fno-lto ${mmix_scheduling_options}
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
    set(mmix_async_options)
    if(LIBC_MMIX_BUILD_ASYNC_CANCELLATION)
      set(mmix_async_options -DLIBC_MMIX_ASYNC_CANCELLATION)
    endif()
    set(mmix_window_dependency)
    if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
      add_object_library(
        cancellation_point
        # Reuse the private save producer without exporting a public setjmp API.
        SRCS cancellation_point.cpp cancellation_leaf.S
          ${LIBC_SOURCE_DIR}/src/setjmp/mmix/linux/setjmp.cpp
        HDRS cancellation_point.h
        DEPENDS .lifecycle libc.hdr.errno_macros libc.hdr.signal_macros
          libc.src.errno.errno libc.hdr.types.jmp_buf
          libc.hdr.types.sigjmp_buf libc.include.ucontext libc.src.__support.common
          libc.src.setjmp.mmix.jump_restore
          libc.src.signal.linux.mmix.kernel_sigaction
          libc.src.signal.linux.mmix.signal_trampoline
          libc.src.__support.OSUtil.linux.mmix.syscall libc.include.sys_syscall
        COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_async_options}
      )
      set(mmix_window_dependency .cancellation_point)
    endif()
    set(mmix_cleanup_options)
    if(LIBC_MMIX_BUILD_FORCED_EXIT)
      set(mmix_cleanup_options -fexceptions)
    endif()
    add_object_library(
      cleanup
      SRCS cleanup.cpp
      HDRS cleanup.h
      DEPENDS .lifecycle libc.src.__support.OSUtil.linux.mmix.syscall
              libc.include.sys_syscall libc.hdr.signal_macros
      COMPILE_OPTIONS -fno-lto -funwind-tables ${mmix_cleanup_options} ${mmix_async_options}
        -D_LIBUNWIND_IS_NATIVE_ONLY
        -I${LIBC_SOURCE_DIR}/../libunwind/include
    )
    add_object_library(
      unwind_root
      SRCS unwind_root.cpp
      HDRS unwind_root.h
      DEPENDS .lifecycle .cleanup libc.src.__support.OSUtil.linux.mmix.syscall
              libc.include.sys_syscall libc.hdr.signal_macros ${mmix_window_dependency}
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
  if(LIBC_MMIX_BUILD_DEFERRED_CANCELLATION)
    add_object_library(
      cancellation
      SRCS cancellation.cpp
      HDRS cancellation.h
      DEPENDS .lifecycle .unwind_root libc.hdr.errno_macros
        libc.hdr.signal_macros libc.include.pthread
        libc.src.__support.OSUtil.linux.mmix.syscall libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables
        ${mmix_async_options}
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
    ${mmix_robust_dependency}
    libc.src.__support.threads.thread_common
    ${mmix_main_errno_dependency}
    ${mmix_main_lifecycle_dependency}
    libc.src.__support.CPP.limits
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.include.sys_syscall
  COMPILE_OPTIONS ${mmix_robust_options}
)
