if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
endif()

add_object_library(
  posix_timezone
  SRCS
    posix_timezone.cpp
  HDRS
    posix_timezone.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.src.__support.CPP.string_view
    libc.src.__support.macros.config
)

add_header_library(
  time_constants
  HDRS
    time_constants.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.hdr.types.time_t
    libc.include.time
    libc.src.__support.CPP.array
    libc.src.__support.CPP.string_view
)

add_object_library(
  time_utils
  SRCS
    time_utils.cpp
  HDRS
    time_utils.h
  DEPENDS
    .posix_timezone
    .time_constants
    libc.hdr.stdint_proxy
    libc.hdr.types.size_t
    libc.hdr.types.struct_tm
    libc.hdr.types.time_t
    libc.include.time
    libc.src.__support.CPP.limits
    libc.src.__support.CPP.optional
    libc.src.__support.CPP.string_view
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
)

set(time_zone_deps .posix_timezone .time_utils
  libc.hdr.types.struct_tm libc.hdr.types.time_t
  libc.src.__support.CPP.limits
  libc.src.__support.error_or libc.src.__support.macros.config)
set(time_zone_options "")
if(LIBC_TARGET_OS_IS_LINUX)
  list(APPEND time_zone_options -DLIBC_COPT_POSIX_TIMEZONE)
  list(APPEND time_zone_deps
    libc.hdr.func.malloc
    libc.src.__support.CPP.mutex
    libc.src.__support.CPP.new
    libc.src.__support.threads.mutex)
  if(LLVM_LIBC_FULL_BUILD)
    list(APPEND time_zone_deps libc.config.app_h libc.src.stdlib.environ_internal)
  else()
    list(APPEND time_zone_deps libc.hdr.stdlib_overlay)
  endif()
  if("tz_variables" IN_LIST TARGET_ENTRYPOINT_NAME_LIST)
    list(APPEND time_zone_options -DLIBC_COPT_TZ_VARIABLES)
    list(APPEND time_zone_deps .linux.tz_variables)
  endif()
endif()

add_object_library(
  time_zone
  SRCS
    time_zone.cpp
  HDRS
    time_zone.h
  DEPENDS
    ${time_zone_deps}
  COMPILE_OPTIONS
    ${time_zone_options}
)

add_subdirectory(strftime_core) #TODO: Move to top

add_header_library(
  asctime_utils
  HDRS
    asctime_utils.h
  DEPENDS
    .time_constants
    .time_utils
    libc.hdr.errno_macros
    libc.hdr.types.size_t
    libc.hdr.types.struct_tm
    libc.src.__support.error_or
    libc.src.__support.macros.config
    libc.src.__support.printf_core.writer
    libc.src.time.strftime_core.strftime_main
)

add_entrypoint_object(
  asctime
  SRCS
    asctime.cpp
  HDRS
    asctime.h
  DEPENDS
    .asctime_utils
    .time_constants
    libc.include.time
    libc.hdr.types.struct_tm
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  asctime_r
  SRCS
    asctime_r.cpp
  HDRS
    asctime_r.h
  DEPENDS
    .asctime_utils
    .time_constants
    libc.include.time
    libc.hdr.types.struct_tm
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  ctime
  SRCS
    ctime.cpp
  HDRS
    ctime.h
  DEPENDS
    .asctime_utils
    .time_utils
    .time_zone
    .time_constants
    libc.hdr.types.time_t
    libc.include.time
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  ctime_r
  SRCS
    ctime_r.cpp
  HDRS
    ctime_r.h
  DEPENDS
    .asctime_utils
    .time_utils
    .time_zone
    .time_constants
    libc.hdr.types.time_t
    libc.include.time
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  localtime
  SRCS
    localtime.cpp
  HDRS
    localtime.h
  DEPENDS
    .time_utils
    .time_zone
    libc.hdr.types.time_t
    libc.hdr.types.struct_tm
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  localtime_r
  SRCS
    localtime_r.cpp
  HDRS
    localtime_r.h
  DEPENDS
    .time_utils
    .time_zone
    libc.hdr.types.time_t
    libc.hdr.types.struct_tm
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  difftime
  SRCS
    difftime.cpp
  HDRS
    difftime.h
  DEPENDS
    libc.include.time
    libc.hdr.types.time_t
)

add_entrypoint_object(
  gmtime
  SRCS
    gmtime.cpp
  HDRS
    gmtime.h
  DEPENDS
    .time_utils
    libc.include.time
    libc.hdr.types.time_t
    libc.hdr.types.struct_tm
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  gmtime_r
  SRCS
    gmtime_r.cpp
  HDRS
    gmtime_r.h
  DEPENDS
    .time_utils
    libc.include.time
    libc.hdr.types.time_t
    libc.hdr.types.struct_tm
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  mktime
  SRCS
    mktime.cpp
  HDRS
    mktime.h
  DEPENDS
    .time_utils
    .time_zone
    .time_constants
    libc.include.time
    libc.src.errno.errno
    libc.hdr.types.time_t
    libc.hdr.types.struct_tm
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  strftime
  SRCS
    strftime.cpp
  HDRS
    strftime.h
  DEPENDS
    libc.hdr.types.size_t
    libc.hdr.types.struct_tm
    libc.src.time.strftime_core.strftime_main
    libc.src.__support.macros.null_check
    libc.src.__support.printf_core.writer
)

add_entrypoint_object(
  strftime_l
  SRCS
    strftime_l.cpp
  HDRS
    strftime_l.h
  DEPENDS
    libc.hdr.types.locale_t
    libc.hdr.types.size_t
    libc.hdr.types.struct_tm
    libc.src.time.strftime_core.strftime_main
    libc.src.__support.macros.null_check
    libc.src.__support.printf_core.writer
)

add_entrypoint_object(
  strptime
  SRCS
    strptime.cpp
  HDRS
    strptime.h
  DEPENDS
    libc.hdr.types.struct_tm
    libc.src.__support.common
    libc.src.__support.macros.config
)

add_entrypoint_object(
  time
  SRCS
    time.cpp
  HDRS
    time_func.h
  DEPENDS
    libc.hdr.time_macros
    libc.hdr.types.time_t
    libc.src.__support.time.clock_gettime
    libc.src.errno.errno
    libc.hdr.types.struct_tm
)

add_entrypoint_object(
  timespec_get
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.timespec_get
)

add_entrypoint_object(
  clock
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.clock
)

add_entrypoint_object(
  nanosleep
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.nanosleep
)

add_entrypoint_object(
  clock_gettime
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.clock_gettime
)

add_entrypoint_object(
  clock_getres
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.clock_getres
)

add_entrypoint_object(
  clock_settime
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.clock_settime
)

add_entrypoint_object(
  timer_create
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.timer_create
)

add_entrypoint_object(
  timer_delete
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.timer_delete
)

add_entrypoint_object(
  timer_gettime
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.timer_gettime
)

add_entrypoint_object(
  timer_settime
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.timer_settime
)

add_entrypoint_object(
  tz_variables
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.tz_variables
)

add_entrypoint_object(
  tzset
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.tzset
)

add_entrypoint_object(
  clock_getcpuclockid
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.clock_getcpuclockid
)
