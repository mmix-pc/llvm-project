set(cnd_wait_source cnd_wait.cpp)
set(cnd_timedwait_source cnd_timedwait.cpp)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  include(${CMAKE_CURRENT_LIST_DIR}/mmix/Waits.cmake)
endif()

add_entrypoint_object(
  mtx_timedlock
  SRCS
    mtx_timedlock.cpp
  HDRS
    ../mtx_timedlock.h
  DEPENDS
    libc.include.threads
    libc.src.__support.macros.null_check
    libc.src.__support.threads.mutex
    libc.src.__support.time.abs_timeout
)

add_header_library(
  threads_utils
  HDRS
    Futex.h
  DEPENDS
    libc.include.sys_syscall
    libc.include.threads
    libc.src.__support.CPP.atomic
    libc.src.__support.CPP.mutex
    libc.src.__support.OSUtil.osutil
    libc.src.__support.threads.mutex
    libc.src.__support.threads.raw_mutex
    libc.src.__support.threads.linux.futex_utils
)

add_entrypoint_object(
  cnd_init
  SRCS
    cnd_init.cpp
  HDRS
    ../cnd_init.h
  DEPENDS
    libc.include.threads
    libc.src.__support.CPP.new
    libc.src.__support.threads.CndVar
)

add_entrypoint_object(
  cnd_destroy
  SRCS
    cnd_destroy.cpp
  HDRS
    ../cnd_destroy.h
  DEPENDS
    libc.include.threads
    libc.src.__support.threads.CndVar
)

add_entrypoint_object(
  cnd_wait
  SRCS
    ${cnd_wait_source}
  HDRS
    ../cnd_wait.h
  DEPENDS
    libc.include.threads
    libc.src.__support.threads.mutex
    libc.src.__support.threads.CndVar
    ${cnd_wait_dependencies}
  COMPILE_OPTIONS ${cnd_wait_options}
)

add_entrypoint_object(
  cnd_timedwait
  SRCS
    ${cnd_timedwait_source}
  HDRS
    ../cnd_timedwait.h
  DEPENDS
    libc.include.threads
    libc.src.__support.threads.mutex
    libc.src.__support.threads.CndVar
    libc.src.__support.time.abs_timeout
    libc.src.__support.macros.null_check
    ${cnd_wait_dependencies}
  COMPILE_OPTIONS ${cnd_wait_options}
)

add_entrypoint_object(
  cnd_signal
  SRCS
    cnd_signal.cpp
  HDRS
    ../cnd_signal.h
  DEPENDS
    libc.include.threads
    libc.src.__support.threads.CndVar
)

add_entrypoint_object(
  cnd_broadcast
  SRCS
    cnd_broadcast.cpp
  HDRS
    ../cnd_broadcast.h
  DEPENDS
    libc.include.threads
    libc.src.__support.threads.CndVar
)
