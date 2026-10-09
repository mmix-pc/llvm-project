foreach(operation pthread_attr_init pthread_attr_getschedparam pthread_attr_setschedparam pthread_create pthread_setschedparam pthread_getschedparam)
  set(${operation}_source ${operation}.cpp)
endforeach()
foreach(operation pthread_join pthread_cond_wait pthread_cond_timedwait
                  pthread_cond_clockwait)
  set(${operation}_source ${operation}.cpp)
endforeach()
foreach(operation lock trylock unlock destroy)
  set(pthread_mutex_${operation}_source pthread_mutex_${operation}.cpp)
endforeach()
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  include(${CMAKE_CURRENT_LIST_DIR}/linux/mmix/Scheduling.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/linux/mmix/ThreadClocks.cmake)
  include(${CMAKE_CURRENT_LIST_DIR}/linux/mmix/Cancellation.cmake)
  if(LIBC_MMIX_BUILD_ROBUST_MUTEX)
    set(mmix_robust_dependency libc.src.__support.threads.linux.mmix.robust_mutex)
    foreach(operation lock trylock unlock destroy consistent)
      set(pthread_mutex_${operation}_source linux/mmix/robust_pthread_mutex_${operation}.cpp)
    endforeach()
    add_entrypoint_object(pthread_mutex_consistent
      SRCS ${pthread_mutex_consistent_source}
      HDRS pthread_mutex_consistent.h
      DEPENDS libc.include.pthread ${mmix_robust_dependency})
    list(APPEND mmix_wait_dependencies ${mmix_robust_dependency})
    list(APPEND mmix_wait_options -DLIBC_MMIX_ROBUST_MUTEX)
    set(mmix_robust_options -DLIBC_MMIX_ROBUST_MUTEX)
    if(LIBC_MMIX_BUILD_PI_MUTEX)
      list(APPEND mmix_robust_options -DLIBC_MMIX_PI_MUTEX)
      foreach(operation getprotocol setprotocol)
        add_entrypoint_object(pthread_mutexattr_${operation}
          SRCS linux/mmix/pthread_mutexattr_${operation}.cpp
          HDRS pthread_mutexattr_${operation}.h linux/mmix/mutex_protocol.h
          DEPENDS libc.include.pthread .pthread_mutexattr libc.hdr.errno_macros)
      endforeach()
    endif()
    if(LIBC_MMIX_BUILD_PROTECT_MUTEX)
      list(APPEND mmix_robust_options -DLIBC_MMIX_PROTECT_MUTEX)
      list(APPEND mmix_wait_options -DLIBC_MMIX_PROTECT_MUTEX)
      list(APPEND mmix_robust_dependency libc.src.__support.threads.linux.mmix.protect_mutex)
      list(APPEND mmix_wait_dependencies libc.src.__support.threads.linux.mmix.protect_mutex)
      foreach(operation lock trylock unlock destroy)
        set_source_files_properties(linux/mmix/robust_pthread_mutex_${operation}.cpp
          PROPERTIES COMPILE_DEFINITIONS LIBC_MMIX_PROTECT_MUTEX)
      endforeach()
      set_source_files_properties(linux/mmix/pthread_mutexattr_setprotocol.cpp
        PROPERTIES COMPILE_DEFINITIONS LIBC_MMIX_PROTECT_MUTEX)
      foreach(operation getprioceiling setprioceiling)
        add_entrypoint_object(pthread_mutexattr_${operation}
          SRCS linux/mmix/pthread_mutexattr_${operation}.cpp
          HDRS pthread_mutexattr_${operation}.h
          DEPENDS libc.include.pthread .pthread_mutexattr ${mmix_robust_dependency})
        add_entrypoint_object(pthread_mutex_${operation}
          SRCS linux/mmix/pthread_mutex_${operation}.cpp
          HDRS pthread_mutex_${operation}.h
          DEPENDS libc.include.pthread ${mmix_robust_dependency})
      endforeach()
    endif()
  endif()
endif()

add_entrypoint_object(
  pthread_attr_init
  SRCS
    ${pthread_attr_init_source}
  HDRS
    pthread_attr_init.h
  DEPENDS
    ${mmix_scheduling_dependency}
    libc.include.pthread
)

add_entrypoint_object(
  pthread_attr_destroy
  SRCS
    pthread_attr_destroy.cpp
  HDRS
    pthread_attr_destroy.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_attr_getdetachstate
  SRCS
    pthread_attr_getdetachstate.cpp
  HDRS
    pthread_attr_getdetachstate.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_attr_setdetachstate
  SRCS
    pthread_attr_setdetachstate.cpp
  HDRS
    pthread_attr_setdetachstate.h
  DEPENDS
    libc.include.pthread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_attr_getguardsize
  SRCS
    pthread_attr_getguardsize.cpp
  HDRS
    pthread_attr_getguardsize.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_attr_setguardsize
  SRCS
    pthread_attr_setguardsize.cpp
  HDRS
    pthread_attr_setguardsize.h
  DEPENDS
    libc.include.pthread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_attr_getschedparam
  SRCS
    ${pthread_attr_getschedparam_source}
  HDRS
    pthread_attr_getschedparam.h
  DEPENDS
    ${mmix_scheduling_dependency}
    libc.include.pthread
)

add_entrypoint_object(
  pthread_attr_setschedparam
  SRCS
    ${pthread_attr_setschedparam_source}
  HDRS
    pthread_attr_setschedparam.h
  DEPENDS
    ${mmix_scheduling_dependency}
    libc.include.pthread
)

add_entrypoint_object(
  pthread_attr_getstacksize
  SRCS
    pthread_attr_getstacksize.cpp
  HDRS
    pthread_attr_getstacksize.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_attr_setstacksize
  SRCS
    pthread_attr_setstacksize.cpp
  HDRS
    pthread_attr_setstacksize.h
  DEPENDS
    libc.include.pthread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_attr_getstack
  SRCS
    pthread_attr_getstack.cpp
  HDRS
    pthread_attr_getstack.h
  DEPENDS
    libc.include.pthread
    libc.src.pthread.pthread_attr_getstacksize
)

add_entrypoint_object(
  pthread_attr_setstack
  SRCS
    pthread_attr_setstack.cpp
  HDRS
    pthread_attr_setstack.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.include.pthread
    libc.src.pthread.pthread_attr_setstacksize
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_condattr_destroy
  SRCS
    pthread_condattr_destroy.cpp
  HDRS
    pthread_condattr_destroy.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_condattr_getclock
  SRCS
    pthread_condattr_getclock.cpp
  HDRS
    pthread_condattr_getclock.h
  DEPENDS
    libc.include.pthread
    libc.hdr.types.clockid_t
)

add_entrypoint_object(
  pthread_condattr_getpshared
  SRCS
    pthread_condattr_getpshared.cpp
  HDRS
    pthread_condattr_getpshared.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_condattr_init
  SRCS
    pthread_condattr_init.cpp
  HDRS
    pthread_condattr_init.h
  DEPENDS
    libc.include.pthread
    libc.include.time
)

add_entrypoint_object(
  pthread_condattr_setclock
  SRCS
    pthread_condattr_setclock.cpp
  HDRS
    pthread_condattr_setclock.h
  DEPENDS
    libc.hdr.time_macros
    libc.hdr.types.clockid_t
    libc.include.pthread
    libc.include.time
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_condattr_setpshared
  SRCS
    pthread_condattr_setpshared.cpp
  HDRS
    pthread_condattr_setpshared.h
  DEPENDS
    libc.include.pthread
    libc.src.errno.errno
)

add_header_library(
  pthread_cond_utils
  HDRS
    pthread_cond_utils.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.time_macros
    libc.include.llvm-libc-types.clockid_t
    libc.include.llvm-libc-types.pthread_cond_t
    libc.include.llvm-libc-types.pthread_mutex_t
    libc.include.llvm-libc-types.struct_timespec
    libc.src.__support.CPP.optional
    libc.src.__support.macros.null_check
    libc.src.__support.macros.optimization
    libc.src.__support.threads.CndVar
    libc.src.__support.threads.mutex
    libc.src.__support.time.abs_timeout
)

add_entrypoint_object(
  pthread_cond_broadcast
  SRCS
    pthread_cond_broadcast.cpp
  HDRS
    pthread_cond_broadcast.h
  DEPENDS
    .pthread_cond_utils
)

add_entrypoint_object(
  pthread_cond_clockwait
  SRCS
    ${pthread_cond_clockwait_source}
  HDRS
    pthread_cond_clockwait.h
  DEPENDS
    ${mmix_wait_dependencies}
    .pthread_cond_utils
    libc.hdr.errno_macros
    libc.include.llvm-libc-types.clockid_t
    libc.include.llvm-libc-types.pthread_cond_t
    libc.include.llvm-libc-types.pthread_mutex_t
    libc.include.llvm-libc-types.struct_timespec
  COMPILE_OPTIONS ${mmix_wait_options}
)

add_entrypoint_object(
  pthread_cond_destroy
  SRCS
    pthread_cond_destroy.cpp
  HDRS
    pthread_cond_destroy.h
  DEPENDS
    .pthread_cond_utils
)

set(pthread_shared_options)
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
   LIBC_MMIX_BUILD_PROCESS_SHARED)
  set(pthread_shared_options -DLIBC_MMIX_PROCESS_SHARED)
endif()
set(pthread_cond_init_source pthread_cond_init.cpp)
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(pthread_cond_init_source linux/mmix/pthread_cond_init.cpp)
endif()
add_entrypoint_object(
  pthread_cond_init
  SRCS
    ${pthread_cond_init_source}
  HDRS
    pthread_cond_init.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.time_macros
    libc.include.llvm-libc-macros.pthread_macros
    libc.include.llvm-libc-types.pthread_cond_t
    libc.include.llvm-libc-types.pthread_condattr_t
    libc.src.__support.CPP.new
    libc.src.__support.macros.null_check
    libc.src.__support.threads.CndVar
  COMPILE_OPTIONS ${pthread_shared_options}
)

add_entrypoint_object(
  pthread_cond_signal
  SRCS
    pthread_cond_signal.cpp
  HDRS
    pthread_cond_signal.h
  DEPENDS
    .pthread_cond_utils
)

add_entrypoint_object(
  pthread_cond_timedwait
  SRCS
    ${pthread_cond_timedwait_source}
  HDRS
    pthread_cond_timedwait.h
  DEPENDS
    ${mmix_wait_dependencies}
    .pthread_cond_utils
    libc.include.llvm-libc-types.pthread_cond_t
    libc.include.llvm-libc-types.pthread_mutex_t
    libc.include.llvm-libc-types.struct_timespec
  COMPILE_OPTIONS ${mmix_wait_options}
)

add_entrypoint_object(
  pthread_cond_wait
  SRCS
    ${pthread_cond_wait_source}
  HDRS
    pthread_cond_wait.h
  DEPENDS
    ${mmix_wait_dependencies}
    .pthread_cond_utils
    libc.src.__support.CPP.optional
    libc.include.llvm-libc-types.pthread_cond_t
    libc.include.llvm-libc-types.pthread_mutex_t
  COMPILE_OPTIONS ${mmix_wait_options}
)

add_header_library(
  pthread_mutexattr
  HDRS
    pthread_mutexattr.h
  DEPENDS
    libc.src.__support.common
    libc.include.pthread
)

add_entrypoint_object(
  pthread_mutexattr_init
  SRCS
    pthread_mutexattr_init.cpp
  HDRS
    pthread_mutexattr_init.h
  DEPENDS
    .pthread_mutexattr
    libc.include.pthread
)

add_entrypoint_object(
  pthread_mutexattr_gettype
  SRCS
    pthread_mutexattr_gettype.cpp
  HDRS
    pthread_mutexattr_gettype.h
  DEPENDS
    .pthread_mutexattr
    libc.include.pthread
)

add_entrypoint_object(
  pthread_mutexattr_settype
  SRCS
    pthread_mutexattr_settype.cpp
  HDRS
    pthread_mutexattr_settype.h
  DEPENDS
    .pthread_mutexattr
    libc.include.pthread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_mutexattr_destroy
  SRCS
    pthread_mutexattr_destroy.cpp
  HDRS
    pthread_mutexattr_destroy.h
  DEPENDS
    .pthread_mutexattr
    libc.src.errno.errno
    libc.include.pthread
)

add_entrypoint_object(
  pthread_mutexattr_getrobust
  SRCS
    pthread_mutexattr_getrobust.cpp
  HDRS
    pthread_mutexattr_getrobust.h
  DEPENDS
    .pthread_mutexattr
    libc.include.pthread
)

add_entrypoint_object(
  pthread_mutexattr_setrobust
  SRCS
    pthread_mutexattr_setrobust.cpp
  HDRS
    pthread_mutexattr_setrobust.h
  DEPENDS
    .pthread_mutexattr
    libc.include.pthread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_mutexattr_getpshared
  SRCS
    pthread_mutexattr_getpshared.cpp
  HDRS
    pthread_mutexattr_getpshared.h
  DEPENDS
    .pthread_mutexattr
    libc.include.pthread
)

add_entrypoint_object(
  pthread_mutexattr_setpshared
  SRCS
    pthread_mutexattr_setpshared.cpp
  HDRS
    pthread_mutexattr_setpshared.h
  DEPENDS
    .pthread_mutexattr
    libc.include.pthread
    libc.src.errno.errno
)

set(pthread_barrier_init_source pthread_barrier_init.cpp)
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(pthread_barrier_init_source linux/mmix/pthread_barrier_init.cpp)
  foreach(operation init destroy getpshared setpshared)
    add_entrypoint_object(
      pthread_barrierattr_${operation}
      SRCS linux/mmix/pthread_barrierattr_${operation}.cpp
      HDRS pthread_barrierattr_${operation}.h
      DEPENDS libc.include.pthread libc.hdr.errno_macros
    )
  endforeach()
endif()
add_entrypoint_object(
  pthread_barrier_init
  SRCS
    ${pthread_barrier_init_source}
  HDRS
    pthread_barrier_init.h
  DEPENDS
    libc.src.errno.errno
    libc.include.pthread
    libc.src.__support.threads.linux.barrier
  COMPILE_OPTIONS ${pthread_shared_options}
)

add_entrypoint_object(
  pthread_barrier_destroy
  SRCS
    pthread_barrier_destroy.cpp
  HDRS
    pthread_barrier_destroy.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.linux.barrier
)

add_entrypoint_object(
  pthread_barrier_wait
  SRCS
    pthread_barrier_wait.cpp
  HDRS
    pthread_barrier_wait.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.linux.barrier
)

set(pthread_mutex_init_source pthread_mutex_init.cpp)
set(pthread_mutex_init_options)
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(pthread_mutex_init_source linux/mmix/pthread_mutex_init.cpp)
  if(LIBC_MMIX_BUILD_CXX_RUNTIME_DEPENDENCIES)
    set(pthread_mutex_init_options -DLIBC_MMIX_PRIVATE_RECURSIVE_MUTEX)
  endif()
  if(LIBC_MMIX_BUILD_MUTEX_TYPES)
    list(APPEND pthread_mutex_init_options -DLIBC_MMIX_PUBLIC_MUTEX_TYPES)
  endif()
endif()
add_entrypoint_object(
  pthread_mutex_init
  SRCS
    ${pthread_mutex_init_source}
  HDRS
    pthread_mutex_init.h
  DEPENDS
    .pthread_mutexattr
    libc.hdr.errno_macros
    libc.src.errno.errno
    libc.include.pthread
    libc.src.__support.threads.mutex
    ${mmix_robust_dependency}
  COMPILE_OPTIONS ${pthread_mutex_init_options}
    ${pthread_shared_options} ${mmix_robust_options}
)

add_entrypoint_object(
  pthread_mutex_destroy
  SRCS
    ${pthread_mutex_destroy_source}
  HDRS
    pthread_mutex_destroy.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.mutex
    ${mmix_robust_dependency}
)

add_entrypoint_object(
  pthread_mutex_lock
  SRCS
    ${pthread_mutex_lock_source}
  HDRS
    pthread_mutex_lock.h
  DEPENDS
    libc.hdr.errno_macros
    libc.include.pthread
    libc.src.__support.threads.mutex
    ${mmix_robust_dependency}
)

add_entrypoint_object(
  pthread_mutex_trylock
  SRCS
    ${pthread_mutex_trylock_source}
  HDRS
    pthread_mutex_trylock.h
  DEPENDS
    libc.hdr.errno_macros
    libc.include.pthread
    libc.src.__support.threads.mutex
    ${mmix_robust_dependency}
)

if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  foreach(operation timedlock clocklock)
    add_entrypoint_object(
      pthread_mutex_${operation}
      SRCS linux/mmix/pthread_mutex_${operation}.cpp
      HDRS pthread_mutex_${operation}.h linux/mmix/mutex_timeout.h
      DEPENDS
        libc.include.pthread
        libc.hdr.errno_macros
        libc.hdr.time_macros
        libc.src.__support.threads.mutex
        ${mmix_robust_dependency}
      COMPILE_OPTIONS ${mmix_robust_options}
    )
  endforeach()
endif()

add_entrypoint_object(
  pthread_mutex_unlock
  SRCS
    ${pthread_mutex_unlock_source}
  HDRS
    pthread_mutex_unlock.h
  DEPENDS
    libc.hdr.errno_macros
    libc.include.pthread
    libc.src.__support.libc_assert
    libc.src.__support.threads.mutex
    ${mmix_robust_dependency}
)

add_entrypoint_object(
  pthread_create
  SRCS
    ${pthread_create_source}
  HDRS
    pthread_create.h
  DEPENDS
    ${mmix_scheduling_dependency}
    libc.include.pthread
    libc.src.__support.threads.thread
    libc.src.pthread.pthread_attr_destroy
    libc.src.pthread.pthread_attr_init
    libc.src.pthread.pthread_attr_getdetachstate
    libc.src.pthread.pthread_attr_getguardsize
    libc.src.pthread.pthread_attr_getstack
    libc.src.errno.errno
  COMPILE_OPTIONS
    ${libc_opt_high_flag}
    -fno-omit-frame-pointer
)

add_entrypoint_object(
  pthread_join
  SRCS
    ${pthread_join_source}
  HDRS
    pthread_join.h
  DEPENDS
    ${mmix_wait_dependencies}
    libc.include.pthread
    libc.src.__support.threads.thread
  COMPILE_OPTIONS ${mmix_wait_options}
)

set(pthread_detach_source pthread_detach.cpp)
set(pthread_detach_dependencies libc.src.__support.threads.thread)
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(pthread_detach_source linux/mmix/pthread_detach.cpp)
  set(pthread_detach_dependencies
    libc.src.__support.threads.linux.mmix.thread_detach
    libc.src.__support.OSUtil.linux.mmix.syscall
    libc.hdr.signal_macros
    libc.include.sys_syscall)
endif()

add_entrypoint_object(
  pthread_detach
  SRCS
    ${pthread_detach_source}
  HDRS
    pthread_detach.h
  DEPENDS
    libc.include.pthread
    ${pthread_detach_dependencies}
)

add_entrypoint_object(
  pthread_equal
  SRCS
    pthread_equal.cpp
  HDRS
    pthread_equal.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.thread
)

set(pthread_exit_source pthread_exit.cpp)
set(pthread_exit_dependencies libc.src.__support.threads.thread)
set(pthread_exit_options)
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix"
   AND LIBC_MMIX_BUILD_FORCED_EXIT)
  set(pthread_exit_source linux/mmix/pthread_exit.cpp)
  set(pthread_exit_dependencies libc.src.__support.threads.linux.mmix.unwind_root)
  set(pthread_exit_options -fno-lto -fexceptions -funwind-tables)
endif()

add_entrypoint_object(
  pthread_exit
  SRCS
    ${pthread_exit_source}
  HDRS
    pthread_exit.h
  DEPENDS
    libc.include.pthread
    ${pthread_exit_dependencies}
  COMPILE_OPTIONS ${pthread_exit_options}
)

add_entrypoint_object(
  pthread_self
  SRCS
    pthread_self.cpp
  HDRS
    pthread_self.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.thread
)

add_entrypoint_object(
  pthread_setname_np
  SRCS
    pthread_setname_np.cpp
  HDRS
    pthread_setname_np.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.CPP.span
    libc.src.__support.CPP.stringstream
    libc.src.__support.threads.thread
)

add_entrypoint_object(
  pthread_getname_np
  SRCS
    pthread_getname_np.cpp
  HDRS
    pthread_getname_np.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.CPP.span
    libc.src.__support.CPP.stringstream
    libc.src.__support.threads.thread
)

add_entrypoint_object(
  pthread_getthreadid_np
  SRCS
    pthread_getthreadid_np.cpp
  HDRS
    pthread_getthreadid_np.h
  DEPENDS
    libc.include.llvm-libc-types.pthread_id_np_t
    libc.src.__support.threads.thread
)

add_entrypoint_object(
  pthread_getunique_np
  SRCS
    pthread_getunique_np.cpp
  HDRS
    pthread_getunique_np.h
  DEPENDS
    libc.hdr.errno_macros
    libc.include.llvm-libc-types.pthread_id_np_t
    libc.include.llvm-libc-types.pthread_t
)

add_entrypoint_object(
  pthread_key_create
  SRCS
    pthread_key_create.cpp
  HDRS
    pthread_key_create.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.thread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_key_delete
  SRCS
    pthread_key_delete.cpp
  HDRS
    pthread_key_delete.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.thread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_getspecific
  SRCS
    pthread_getspecific.cpp
  HDRS
    pthread_getspecific.h
  DEPENDS
    libc.include.pthread
    libc.src.errno.errno
    libc.src.__support.threads.thread
)

add_entrypoint_object(
  pthread_setspecific
  SRCS
    pthread_setspecific.cpp
  HDRS
    pthread_setspecific.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.thread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_rwlockattr_destroy
  SRCS
    pthread_rwlockattr_destroy.cpp
  HDRS
    pthread_rwlockattr_destroy.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_rwlockattr_getkind_np
  SRCS
    pthread_rwlockattr_getkind_np.cpp
  HDRS
    pthread_rwlockattr_getkind_np.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_rwlockattr_getpshared
  SRCS
    pthread_rwlockattr_getpshared.cpp
  HDRS
    pthread_rwlockattr_getpshared.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_rwlockattr_init
  SRCS
    pthread_rwlockattr_init.cpp
  HDRS
    pthread_rwlockattr_init.h
  DEPENDS
    libc.include.pthread
)

add_entrypoint_object(
  pthread_rwlockattr_setkind_np
  SRCS
    pthread_rwlockattr_setkind_np.cpp
  HDRS
    pthread_rwlockattr_setkind_np.h
  DEPENDS
    libc.include.pthread
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_rwlockattr_setpshared
  SRCS
    pthread_rwlockattr_setpshared.cpp
  HDRS
    pthread_rwlockattr_setpshared.h
  DEPENDS
    libc.include.pthread
    libc.src.errno.errno
)

set(pthread_rwlock_target_headers)
foreach(operation init timedrdlock timedwrlock clockrdlock clockwrlock)
  set(pthread_rwlock_${operation}_source pthread_rwlock_${operation}.cpp)
endforeach()
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(pthread_rwlock_target_headers linux/mmix/rwlock_timeout.h)
  foreach(operation init timedrdlock timedwrlock clockrdlock clockwrlock)
    set(pthread_rwlock_${operation}_source linux/mmix/pthread_rwlock_${operation}.cpp)
  endforeach()
endif()
add_entrypoint_object(
  pthread_rwlock_init
  SRCS
    ${pthread_rwlock_init_source}
  HDRS
    pthread_rwlock_init.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
    libc.src.__support.CPP.new
  COMPILE_OPTIONS ${pthread_shared_options}
)

add_entrypoint_object(
  pthread_rwlock_tryrdlock
  SRCS
    pthread_rwlock_tryrdlock.cpp
  HDRS
    pthread_rwlock_tryrdlock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
)

add_entrypoint_object(
  pthread_rwlock_trywrlock
  SRCS
    pthread_rwlock_trywrlock.cpp
  HDRS
    pthread_rwlock_trywrlock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_rwlock_clockrdlock
  SRCS
    ${pthread_rwlock_clockrdlock_source}
  HDRS
    pthread_rwlock_clockrdlock.h
    ${pthread_rwlock_target_headers}
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
)

add_entrypoint_object(
  pthread_rwlock_clockwrlock
  SRCS
    ${pthread_rwlock_clockwrlock_source}
  HDRS
    pthread_rwlock_clockwrlock.h
    ${pthread_rwlock_target_headers}
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
)

add_entrypoint_object(
  pthread_rwlock_timedrdlock
  SRCS
    ${pthread_rwlock_timedrdlock_source}
  HDRS
    pthread_rwlock_timedrdlock.h
    ${pthread_rwlock_target_headers}
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_rwlock_timedwrlock
  SRCS
    ${pthread_rwlock_timedwrlock_source}
  HDRS
    pthread_rwlock_timedwrlock.h
    ${pthread_rwlock_target_headers}
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
)

add_entrypoint_object(
  pthread_rwlock_rdlock
  SRCS
    pthread_rwlock_rdlock.cpp
  HDRS
    pthread_rwlock_rdlock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
)

add_entrypoint_object(
  pthread_rwlock_wrlock
  SRCS
    pthread_rwlock_wrlock.cpp
  HDRS
    pthread_rwlock_wrlock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
)

add_entrypoint_object(
  pthread_rwlock_unlock
  SRCS
    pthread_rwlock_unlock.cpp
  HDRS
    pthread_rwlock_unlock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_rwlock_destroy
  SRCS
    pthread_rwlock_destroy.cpp
  HDRS
    pthread_rwlock_destroy.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.unix_rwlock
)

set(pthread_spin_init_source pthread_spin_init.cpp)
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(pthread_spin_init_source linux/mmix/pthread_spin_init.cpp)
endif()
add_entrypoint_object(
  pthread_spin_init
  SRCS
    ${pthread_spin_init_source}
  HDRS
    pthread_spin_init.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.spin_lock
    libc.src.__support.threads.identifier
    libc.hdr.errno_macros
  COMPILE_OPTIONS ${pthread_shared_options}
)

add_entrypoint_object(
  pthread_spin_destroy
  SRCS
    pthread_spin_destroy.cpp
  HDRS
    pthread_spin_destroy.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.spin_lock
    libc.src.__support.threads.identifier
    libc.hdr.errno_macros
)

add_entrypoint_object(
  pthread_spin_lock
  SRCS
    pthread_spin_lock.cpp
  HDRS
    pthread_spin_lock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.spin_lock
    libc.src.__support.threads.identifier
    libc.hdr.errno_macros
)

add_entrypoint_object(
  pthread_spin_trylock
  SRCS
    pthread_spin_trylock.cpp
  HDRS
    pthread_spin_trylock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.spin_lock
    libc.src.__support.threads.identifier
    libc.hdr.errno_macros
)

add_entrypoint_object(
  pthread_spin_unlock
  SRCS
    pthread_spin_unlock.cpp
  HDRS
    pthread_spin_unlock.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.spin_lock
    libc.src.__support.threads.identifier
    libc.hdr.errno_macros
)

set(pthread_once_source pthread_once.cpp)
set(pthread_once_options)
if(LIBC_MMIX_BUILD_COORDINATED_FORK)
  list(APPEND pthread_once_options -DLIBC_MMIX_COORDINATED_FORK)
endif()
if(LIBC_TARGET_OS STREQUAL "linux" AND LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(pthread_once_source linux/mmix/pthread_once.cpp)
  set(pthread_once_dependency libc.src.__support.threads.linux.mmix.once)
  if(LIBC_MMIX_BUILD_FORCED_EXIT)
    list(APPEND pthread_once_options -fno-lto -fexceptions -funwind-tables)
  endif()
endif()
add_entrypoint_object(
  pthread_once
  SRCS
    ${pthread_once_source}
  HDRS
    pthread_once.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.callonce
    ${pthread_once_dependency}
  COMPILE_OPTIONS ${pthread_once_options}
)

set(pthread_atfork_source pthread_atfork.cpp)
if(LIBC_MMIX_BUILD_COORDINATED_FORK)
  set(pthread_atfork_source linux/mmix/pthread_atfork.cpp)
  set(pthread_atfork_dependencies libc.src.__support.threads.linux.mmix.fork_runtime)
endif()
add_entrypoint_object(
  pthread_atfork
  SRCS
    ${pthread_atfork_source}
  HDRS
    pthread_atfork.h
  DEPENDS
    libc.include.pthread
    libc.src.__support.threads.fork_callbacks
    ${pthread_atfork_dependencies}
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_setschedparam
  SRCS
    ${pthread_setschedparam_source}
  HDRS
    pthread_setschedparam.h
  DEPENDS
    ${mmix_scheduling_dependency}
    libc.hdr.types.pthread_t
    libc.hdr.types.struct_sched_param
    libc.src.__support.macros.null_check
    libc.src.__support.threads.thread
)

add_entrypoint_object(
  pthread_getschedparam
  SRCS
    ${pthread_getschedparam_source}
  HDRS
    pthread_getschedparam.h
  DEPENDS
    ${mmix_scheduling_dependency}
    libc.hdr.types.pthread_t
    libc.hdr.types.struct_sched_param
    libc.src.__support.macros.null_check
    libc.src.__support.threads.thread
)

add_entrypoint_object(
  pthread_getstack_np
  SRCS
    pthread_getstack_np.cpp
  HDRS
    pthread_getstack_np.h
  DEPENDS
    libc.hdr.types.pthread_t
    libc.hdr.types.size_t
    libc.src.__support.common
    libc.src.__support.macros.config
    libc.src.__support.macros.null_check
    libc.src.__support.threads.thread
)
