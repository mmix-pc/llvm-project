if(NOT TARGET libc.src.__support.OSUtil.osutil)
  message(STATUS "libc.src.__support.OSUtil.osutil is not available. "
    "Skip libc.src.time.linux.* targets.")
  return()
endif()

set(nanosleep_source nanosleep.cpp)
set(timer_create_source timer_create.cpp)
set(timer_delete_source timer_delete.cpp)
set(timer_settime_source timer_settime.cpp)
set(timer_create_target_dependencies)
set(timer_service_dependencies)
set(timer_service_options)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(timer_create_source mmix/timer_create.cpp)
  set(timer_create_target_dependencies libc.src.signal.linux.mmix.private_signals)
  if(LIBC_MMIX_BUILD_DEFERRED_CANCELLATION AND LIBC_MMIX_BUILD_PTHREAD_SCHEDULING)
    set(timer_delete_source mmix/timer_delete.cpp)
    set(timer_settime_source mmix/timer_settime.cpp)
    set(timer_service_dependencies
      libc.src.__support.threads.linux.mmix.notification_service)
    set(timer_service_options -DLIBC_MMIX_MANAGED_NOTIFICATIONS
      -fexceptions -funwind-tables)
  endif()
  include(${LIBC_SOURCE_DIR}/config/linux/mmix/CancellationPoints.cmake)
  mmix_select_cancellation_points(nanosleep)
endif()

add_entrypoint_object(
  timespec_get
  SRCS
    timespec_get.cpp
  HDRS
    ../timespec_get.h
  DEPENDS
    libc.hdr.time_macros
    libc.hdr.types.struct_timespec
    libc.src.__support.time.clock_gettime
    libc.src.errno.errno
)

add_entrypoint_object(
  clock
  SRCS
    clock.cpp
  HDRS
    ../clock.h
  DEPENDS
    libc.hdr.time_macros
    libc.hdr.types.clock_t
    libc.src.__support.time.units
    libc.src.__support.time.clock_gettime
    libc.src.__support.CPP.limits
    libc.src.errno.errno
)

add_entrypoint_object(
  nanosleep
  SRCS
    ${nanosleep_source}
  HDRS
    ../nanosleep.h
  DEPENDS
    libc.hdr.types.struct_timespec
    libc.src.__support.OSUtil.linux.syscall_wrappers.nanosleep
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  clock_gettime
  SRCS
    clock_gettime.cpp
  HDRS
    ../clock_gettime.h
  DEPENDS
    libc.hdr.types.clockid_t
    libc.hdr.types.struct_timespec
    libc.src.__support.time.clock_gettime
    libc.src.errno.errno
)

add_entrypoint_object(
  clock_settime
  SRCS
    clock_settime.cpp
  HDRS
    ../clock_settime.h
  DEPENDS
    libc.hdr.types.clockid_t
    libc.hdr.types.struct_timespec
    libc.src.__support.time.clock_settime
    libc.src.errno.errno
)

add_entrypoint_object(
  clock_getres
  SRCS clock_getres.cpp
  HDRS ../clock_getres.h
  DEPENDS
    libc.hdr.types.clockid_t
    libc.hdr.types.struct_timespec
    libc.src.__support.OSUtil.linux.syscall_wrappers.clock_getres
    libc.src.__support.common
    libc.src.errno.errno
)

add_entrypoint_object(
  timer_create
  SRCS
    ${timer_create_source}
  HDRS
    ../timer_create.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.signal_macros
    libc.hdr.types.clockid_t
    libc.hdr.types.struct_sigevent
    libc.hdr.types.timer_t
    ${timer_create_target_dependencies}
    ${timer_service_dependencies}
    libc.src.__support.OSUtil.linux.syscall_wrappers.timer_create
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.errno.errno
  COMPILE_OPTIONS ${timer_service_options}
)

add_entrypoint_object(
  timer_delete
  SRCS
    ${timer_delete_source}
  HDRS
    ../timer_delete.h
  DEPENDS
    libc.hdr.types.timer_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.timer_delete
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.errno.errno
    ${timer_service_dependencies}
  COMPILE_OPTIONS ${timer_service_options}
)

add_entrypoint_object(
  timer_gettime
  SRCS
    timer_gettime.cpp
  HDRS
    ../timer_gettime.h
  DEPENDS
    libc.hdr.types.struct_itimerspec
    libc.hdr.types.timer_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.timer_gettime
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.errno.errno
)

add_entrypoint_object(
  timer_settime
  SRCS
    ${timer_settime_source}
  HDRS
    ../timer_settime.h
  DEPENDS
    libc.hdr.types.struct_itimerspec
    libc.hdr.types.timer_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.timer_settime
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.errno.errno
    ${timer_service_dependencies}
  COMPILE_OPTIONS ${timer_service_options}
)

add_entrypoint_object(
  timer_getoverrun
  SRCS timer_getoverrun.cpp
  HDRS ../timer_getoverrun.h
  DEPENDS
    libc.hdr.types.timer_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.timer_getoverrun
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.errno.errno
)

add_entrypoint_object(
  tz_variables
  SRCS
    tz_variables.cpp
  HDRS
    ../tz_variables.h
  DEPENDS
    libc.src.__support.macros.config
)

add_entrypoint_object(
  tzset
  SRCS
    tzset.cpp
  HDRS
    ../tzset.h
  DEPENDS
    libc.src.__support.macros.config
    libc.src.__support.libc_errno
    libc.src.time.time_zone
)

add_entrypoint_object(
  clock_getcpuclockid
  SRCS
    clock_getcpuclockid.cpp
  HDRS
    ../clock_getcpuclockid.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.clockid_t
    libc.hdr.types.pid_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.clock_getres
    libc.src.__support.common
    libc.src.__support.macros.config
)

if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(clock_sleep_dependencies libc.src.__support.OSUtil.osutil)
  set(clock_sleep_options)
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    list(APPEND clock_sleep_dependencies
      libc.src.__support.threads.linux.mmix.cancellation_point)
    list(APPEND clock_sleep_options
      -DLIBC_MMIX_CLOCK_SLEEP_CANCELLATION -fexceptions -funwind-tables)
  endif()
  add_entrypoint_object(
    clock_nanosleep
    SRCS mmix/clock_nanosleep.cpp
    HDRS ../clock_nanosleep.h
    DEPENDS
      libc.hdr.errno_macros
      libc.hdr.time_macros
      libc.hdr.types.clockid_t
      libc.hdr.types.struct_timespec
      libc.src.__support.common
      libc.include.sys_syscall
      ${clock_sleep_dependencies}
    COMPILE_OPTIONS ${clock_sleep_options}
  )
endif()
