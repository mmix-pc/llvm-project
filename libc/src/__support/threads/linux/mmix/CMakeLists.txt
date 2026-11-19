add_header_library(
  stack_policy
  HDRS stack_policy.h
  DEPENDS libc.hdr.types.size_t libc.src.__support.macros.config
)

add_header_library(
  credentials_h
  HDRS credentials.h
  DEPENDS libc.src.__support.macros.config libc.hdr.stdint_proxy
)

add_header_library(
  aio_transfer
  HDRS aio_transfer.h
  DEPENDS .aio_input libc.hdr.sys_socket_macros libc.hdr.sys_stat_macros
    libc.hdr.types.socklen_t
    libc.src.__support.OSUtil.linux.stat.kernel_statx_types
    libc.src.__support.OSUtil.linux.syscall_wrappers.fstatfs
    libc.src.__support.OSUtil.linux.syscall_wrappers.statx
)

add_header_library(
  aio_input
  HDRS aio_input.h
  DEPENDS libc.include.aio libc.hdr.errno_macros libc.hdr.fcntl_macros
    libc.hdr.limits_macros
    libc.hdr.signal_macros
    libc.hdr.stdint_proxy libc.src.__support.CPP.new
    libc.src.__support.macros.attributes libc.src.__support.macros.config
)

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

add_object_library(
  fork_transport
  SRCS fork_transport.cpp
  HDRS fork_transport.h
  DEPENDS
    libc.hdr.types.pid_t libc.hdr.errno_macros libc.hdr.signal_macros
    libc.src.__support.CPP.limits libc.src.__support.threads.thread_common
    libc.src.__support.OSUtil.osutil libc.include.sys_syscall
  COMPILE_OPTIONS -fno-lto
)

if(LIBC_MMIX_ENABLE_STATIC_TLS)
  include(${CMAKE_CURRENT_LIST_DIR}/../../../../../config/linux/mmix/RuntimeMode.cmake)
  mmix_check_runtime_mode()
  set(mmix_callback_ownership_dependency)
  set(mmix_cleanup_ownership_dependency)
  if(LIBC_MMIX_BUILD_SHARED_LIBC)
    add_header_library(
      cleanup_ownership
      HDRS cleanup_ownership.h
      DEPENDS libc.src.__support.OSUtil.linux.loader_services
    )
    set(mmix_cleanup_ownership_dependency .cleanup_ownership .callback_ownership
        libc.src.__support.CPP.new)
    add_header_library(
      callback_ownership
      HDRS callback_ownership.h
      # These declarations are linked by shared libc composition. Depending on
      # the cancellation object here would cycle through its cleanup consumer.
      DEPENDS libc.src.__support.OSUtil.linux.loader_services
    )
    set(mmix_callback_ownership_dependency .callback_ownership)
  endif()
  if(LIBC_MMIX_BUILD_COORDINATED_FORK)
    set(mmix_fork_unwind_dependency)
    set(mmix_fork_unwind_options)
    set(mmix_fork_robust_options)
    if(LIBC_MMIX_BUILD_ROBUST_MUTEX)
      set(mmix_fork_robust_options -DLIBC_MMIX_ROBUST_MUTEX)
    endif()
    if(LIBC_MMIX_BUILD_UNWIND_ROOTS)
      set(mmix_fork_unwind_dependency .unwind_root)
      set(mmix_fork_unwind_options -DLIBC_MMIX_UNWIND_ROOTS -funwind-tables)
    endif()
    if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
      list(APPEND mmix_fork_unwind_dependency .cancellation_point)
    endif()
    set(mmix_loader_fork_dependency)
    if(LIBC_MMIX_BUILD_SHARED_LIBC)
      set(mmix_loader_fork_dependency libc.src.dlfcn.linux.mmix.loading)
    endif()
    add_object_library(
      fork_runtime
      SRCS fork_runtime.cpp atfork_callbacks.cpp
      HDRS fork_runtime.h atfork_callbacks.h
      DEPENDS .lifecycle .once .main_thread .fork_transport .thread_create .robust_mutex .credentials
        libc.src.__support.File.file libc.src.stdlib.environ_internal
        libc.src.stdlib.linux.mmix.allocator libc.src.__support.OSUtil.osutil
        libc.src.stdio.stdin libc.src.stdio.stdout libc.src.stdio.stderr
        libc.src.__support.CPP.new libc.include.sys_syscall ${mmix_loader_fork_dependency}
        ${mmix_callback_ownership_dependency} ${mmix_fork_unwind_dependency}
      COMPILE_OPTIONS -fno-lto ${mmix_fork_unwind_options} ${mmix_fork_robust_options}
    )
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_CLOCKS)
    add_object_library(
      thread_clock
      SRCS thread_clock.cpp thread_clock_kernel.cpp
      HDRS thread_clock.h
      DEPENDS .lifecycle libc.hdr.signal_macros libc.hdr.types.clockid_t
        libc.hdr.types.struct_timespec libc.src.__support.CPP.bit
        libc.src.__support.OSUtil.osutil libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto
    )
  endif()
  if(LIBC_MMIX_BUILD_CONCURRENCY_HINTS)
    add_object_library(
      concurrency_hint
      SRCS concurrency_hint.cpp
      HDRS concurrency_hint.h
      DEPENDS libc.src.__support.CPP.atomic libc.hdr.errno_macros
      COMPILE_OPTIONS -fno-lto
    )
  endif()
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
      COMPILE_OPTIONS -fno-lto -ftls-model=${mmix_runtime_tls_model} ${mmix_scheduling_options}
    )
  endif()
  if(LIBC_MMIX_BUILD_PROTECT_MUTEX)
    add_object_library(
      protect_mutex
      SRCS protect_mutex.cpp robust_protect_mutex.cpp
      HDRS protect_mutex.h robust_protect_mutex.h priority_protection_state.h mutex_flags.h
      DEPENDS .priority_protection .lifecycle .robust_mutex libc.src.__support.OSUtil.osutil
        libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -ftls-model=${mmix_runtime_tls_model}
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
      COMPILE_OPTIONS -fno-lto -ftls-model=${mmix_runtime_tls_model} ${mmix_pi_options}
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
      libc.src.__support.OSUtil.linux.fork_recovery
      libc.src.__support.threads.raw_mutex
      libc.src.__support.threads.thread_attributes
      libc.hdr.errno_macros
  )
  if(LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM")
    add_object_library(
      credentials
      SRCS credentials.cpp
      HDRS credentials.h
      DEPENDS .lifecycle libc.src.errno.errno libc.hdr.errno_macros
        libc.hdr.signal_macros libc.hdr.types.siginfo_t
        libc.src.signal.linux.mmix.kernel_sigaction
        libc.src.signal.linux.mmix.signal_trampoline
        libc.src.__support.OSUtil.linux.mmix.syscall libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto
    )
    list(APPEND mmix_main_lifecycle_dependency .credentials)
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
      .stack_policy
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
  add_object_library(
    notification_callback
    SRCS notification_callback.cpp
    HDRS notification_callback.h
    DEPENDS .resources ${mmix_scheduling_dependency}
      libc.hdr.types.struct_sigevent libc.hdr.errno_macros
      libc.hdr.pthread_macros libc.hdr.signal_macros
      libc.src.__support.CPP.new libc.src.stdlib.malloc libc.src.stdlib.free
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_scheduling_options}
  )
  add_object_library(
    aio_notification
    SRCS aio_notification.cpp
    HDRS aio_notification.h
    DEPENDS .notification_callback .thread_create .thread_reclaim .resources .lifecycle
      libc.hdr.types.siginfo_t libc.hdr.errno_macros libc.hdr.signal_macros
      libc.src.signal.linux.mmix.private_signals
      libc.src.__support.OSUtil.linux.mmix.syscall libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_scheduling_options}
  )
  add_header_library(
    aio_gate
    HDRS aio_gate.h credentials.h
    DEPENDS libc.hdr.signal_macros libc.src.__support.OSUtil.osutil
      libc.src.__support.threads.raw_mutex
  )
  add_header_library(
    aio_suspend_registration
    HDRS aio_suspend_registration.h
      ${LIBC_SOURCE_DIR}/src/setjmp/mmix/linux/jump_cleanup_stack.h
    DEPENDS .aio_gate libc.src.__support.CPP.new
      libc.src.__support.OSUtil.linux.aio_wait_event
      libc.src.__support.threads.aio_suspend_observers
  )
  add_header_library(
    aio_service_registry
    HDRS aio_service_registry.h
    DEPENDS .aio_gate .aio_notification .aio_suspend_registration
      libc.src.__support.OSUtil.linux.aio_engine
      libc.src.__support.threads.aio_storage
  )
  add_object_library(
    notification_mq
    SRCS notification_mq.cpp
    HDRS notification_mq.h
    DEPENDS .notification_callback .thread_create .thread_reclaim
      libc.src.__support.threads.notification_registry
      libc.src.__support.threads.raw_mutex
      libc.src.__support.OSUtil.linux.notification_transport
      libc.src.__support.OSUtil.linux.kernel_sigevent
      libc.src.__support.CPP.new libc.src.stdlib.malloc libc.src.stdlib.free
      libc.include.sys_syscall
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_scheduling_options}
  )
  add_object_library(
    notification_timers
    SRCS notification_timers.cpp
    HDRS notification_timers.h
    DEPENDS .notification_callback .thread_create .credentials
      libc.src.__support.threads.notification_registry
      libc.src.__support.threads.raw_mutex
      libc.src.__support.OSUtil.linux.notification_transport
      libc.src.__support.OSUtil.linux.kernel_sigevent
      libc.src.__support.OSUtil.linux.syscall_wrappers.timer_delete
      libc.src.__support.OSUtil.linux.syscall_wrappers.timer_settime
      libc.src.signal.linux.mmix.private_signals
      libc.hdr.types.struct_itimerspec libc.hdr.types.timer_t
      libc.hdr.types.clockid_t libc.src.__support.CPP.new
      libc.src.stdlib.malloc libc.src.stdlib.free
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_scheduling_options}
  )
  add_object_library(
    aio_dispatcher
    SRCS aio_dispatcher.cpp
    HDRS aio_dispatcher.h
    DEPENDS .thread_create .thread_reclaim .credentials
      libc.src.__support.OSUtil.linux.aio_dispatch_channel
      libc.hdr.signal_macros libc.hdr.types.struct_timespec
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_scheduling_options}
  )
  add_object_library(
    notification_dispatcher
    SRCS notification_dispatcher.cpp
    HDRS notification_dispatcher.h
    DEPENDS .thread_create .thread_reclaim .credentials
      libc.src.__support.OSUtil.linux.notification_channels
      libc.include.llvm-libc-macros.poll-macros libc.hdr.types.struct_pollfd
      libc.hdr.types.struct_timespec
      libc.src.signal.linux.mmix.private_signals
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_scheduling_options}
  )
  if(LIBC_MMIX_BUILD_DEFERRED_CANCELLATION AND LIBC_MMIX_BUILD_PTHREAD_SCHEDULING)
    add_object_library(
      aio_service
      SRCS aio_service.cpp
      HDRS aio_service.h
      DEPENDS .aio_dispatcher .aio_notification .aio_service_registry .aio_input .aio_transfer
        .cancellation .cancellation_point .credentials libc.src.setjmp.mmix.jump_cleanup
        libc.config.app_h libc.include.pthread
        libc.src.__support.CPP.new libc.include.sys_syscall
        libc.src.stdlib.malloc libc.src.stdlib.free
      COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables
        ${mmix_scheduling_options} ${mmix_signal_cancellation_options}
    )
    add_object_library(
      notification_service
      SRCS notification_service.cpp
      HDRS notification_service.h
      DEPENDS .notification_timers .notification_mq .notification_dispatcher
        .cancellation .credentials libc.config.app_h libc.include.pthread
        libc.src.__support.CPP.new libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables
    )
  endif()
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
  set(mmix_thread_create_cancellation_dependency)
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    set(mmix_thread_create_cancellation_dependency .cancellation_point)
  endif()
  add_object_library(
    thread_create
    SRCS thread_create.cpp thread_reaper.cpp
    HDRS thread_create.h thread_reaper.h
    DEPENDS
      .thread_reaper_service .credentials
      ${mmix_scheduling_dependency}
      ${mmix_thread_create_cancellation_dependency}
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
      set(mmix_private_save_source)
      set(mmix_point_jump_dependencies)
      set(mmix_point_jump_options)
      if(LIBC_MMIX_ENABLE_STATIC_TLS)
        set(mmix_point_jump_dependencies libc.src.setjmp.mmix.jump_cleanup)
        set(mmix_point_jump_options -DLIBC_MMIX_JUMP_CLEANUP)
      endif()
      if(NOT LIBC_MMIX_BUILD_THREADED_LIBC OR NOT LIBC_MMIX_BUILD_JUMP_SAVE)
        # Internal archives and public profiles without setjmp still need the
        # hidden save entry. Otherwise the selected public object owns it.
        set(mmix_private_save_source
          ${LIBC_SOURCE_DIR}/src/setjmp/mmix/linux/setjmp.cpp)
      endif()
      add_object_library(
        cancellation_point
        SRCS cancellation_point.cpp cancellation_leaf.S
          ${mmix_private_save_source}
        HDRS cancellation_point.h cancellation_registration.h
        DEPENDS .lifecycle libc.hdr.errno_macros libc.hdr.signal_macros
          libc.hdr.fcntl_macros
          libc.src.errno.errno libc.hdr.types.jmp_buf
          libc.hdr.types.sigjmp_buf libc.include.ucontext libc.src.__support.common
          libc.src.setjmp.mmix.jump_restore
          libc.src.signal.linux.mmix.kernel_sigaction
          libc.src.signal.linux.mmix.signal_trampoline
          libc.src.__support.OSUtil.linux.mmix.syscall libc.include.sys_syscall
          ${mmix_point_jump_dependencies}
          libc.src.__support.CPP.atomic libc.src.__support.CPP.new
        COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables ${mmix_async_options}
          ${mmix_point_jump_options} ${mmix_signal_cancellation_options}
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
              ${mmix_cleanup_ownership_dependency}
      COMPILE_OPTIONS -fno-lto -funwind-tables ${mmix_cleanup_options} ${mmix_async_options}
        ${mmix_signal_cancellation_options}
        -D_LIBUNWIND_IS_NATIVE_ONLY
        -I${LIBC_SOURCE_DIR}/../libunwind/include
        -I${LIBC_SOURCE_DIR}/../libunwind/src
    )
    set(mmix_signal_unwind_sources)
    set(mmix_signal_unwind_dependencies)
    if(LIBC_MMIX_BUILD_SIGNAL_CANCELLATION)
      set(mmix_signal_unwind_sources signal_unwind_landing.S)
      set(mmix_signal_unwind_dependencies libc.src.signal.linux.mmix.signal_entry)
    endif()
    add_object_library(
      unwind_root
      SRCS unwind_root.cpp ${mmix_signal_unwind_sources}
      HDRS unwind_root.h
      DEPENDS .lifecycle .cleanup libc.src.__support.OSUtil.linux.mmix.syscall
              libc.include.sys_syscall libc.hdr.signal_macros ${mmix_window_dependency}
              ${mmix_signal_unwind_dependencies}
      COMPILE_OPTIONS -fno-lto -funwind-tables
        ${mmix_signal_cancellation_options}
        -D_LIBUNWIND_IS_NATIVE_ONLY
        -I${LIBC_SOURCE_DIR}/../libunwind/include
        -I${LIBC_SOURCE_DIR}/../libunwind/src
    )
    set(mmix_root_dependency .unwind_root)
    set(mmix_root_options -DLIBC_MMIX_UNWIND_ROOTS -funwind-tables)
  endif()
  if(LIBC_MMIX_BUILD_TLS_DESTRUCTORS)
    set(mmix_tls_destructor_deps)
    if(LIBC_MMIX_BUILD_SHARED_LIBC)
      list(APPEND mmix_tls_destructor_deps .loader_cancellation
        libc.src.__support.OSUtil.linux.mmix.dynamic_tls)
    endif()
    add_object_library(
      tls_destructors
      SRCS tls_destructors.cpp
      HDRS tls_destructors.h
      DEPENDS .lifecycle libc.src.__support.threads.thread_common
              libc.src.__support.CPP.new libc.src.stdlib.malloc libc.src.stdlib.free
              ${mmix_tls_destructor_deps}
      COMPILE_OPTIONS -fno-lto
    )
  endif()
  if(LIBC_MMIX_BUILD_DEFERRED_CANCELLATION)
    add_object_library(
      loader_cancellation
      SRCS loader_cancellation.cpp
      HDRS loader_cancellation.h
      DEPENDS .cancellation .lifecycle libc.include.pthread libc.hdr.errno_macros
        libc.src.__support.OSUtil.linux.loader_services
        libc.hdr.signal_macros libc.include.sys_syscall
        libc.src.__support.OSUtil.linux.mmix.syscall
      COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables
    )
    add_object_library(
      cancellation
      SRCS cancellation.cpp
      HDRS cancellation.h
      DEPENDS .lifecycle .unwind_root libc.hdr.errno_macros
        libc.hdr.signal_macros libc.include.pthread
        libc.src.__support.OSUtil.linux.mmix.syscall libc.include.sys_syscall
      COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables
        ${mmix_async_options} ${mmix_signal_cancellation_options}
    )
  endif()
  add_object_library(
    thread_finish
    SRCS thread_finish.cpp thread_process_exit.cpp
    HDRS thread_finish.h
    DEPENDS
      libc.hdr.types.pid_t
      ${mmix_once_dependency}
      ${mmix_root_dependency}
      .thread_start .credentials
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
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    add_header_library(
      cancellable_join
      HDRS cancellable_join.h
      DEPENDS .thread_join .cancellation_point
        libc.hdr.signal_macros
        libc.src.__support.OSUtil.linux.mmix.syscall
        libc.include.sys_syscall
    )
  endif()
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
