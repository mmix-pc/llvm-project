foreach(name mq_timedsend mq_timedreceive)
  set(${name}_source ${name}.cpp)
endforeach()
set(mq_notify_source mq_notify.cpp)
set(mq_notify_target_dependencies)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(mq_notify_source mmix/mq_notify.cpp)
  set(mq_notify_target_dependencies libc.src.signal.linux.mmix.private_signals)
  include(${LIBC_SOURCE_DIR}/config/linux/mmix/CancellationPoints.cmake)
  mmix_select_cancellation_points(mq_timedsend mq_timedreceive)
endif()

add_header_library(
  mq_notify_utils
  HDRS
    mq_notify_utils.h
  DEPENDS
    libc.hdr.errno_macros
    libc.include.sys_syscall
    libc.src.__support.OSUtil.linux.kernel_sigevent
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    libc.src.mqueue.types
)

add_entrypoint_object(
  mq_notify
  SRCS
    ${mq_notify_source}
  HDRS
    ../mq_notify.h
  DEPENDS
    .mq_notify_utils
    libc.hdr.types.struct_sigevent
    libc.src.__support.common
    libc.src.mqueue.types
    ${mq_notify_target_dependencies}
)

add_header_library(
  mqueue_utils
  HDRS
    mqueue_utils.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.__support.macros.sanitizer
    libc.src.errno.errno
    libc.src.mqueue.types
)

add_header_library(
  mqueue_timespec
  HDRS
    mqueue_timespec.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.hdr.types.struct_timespec
    libc.src.__support.macros.config
)

add_entrypoint_object(
  mq_open
  SRCS
    mq_open.cpp
  HDRS
    ../mq_open.h
  DEPENDS
    .mqueue_utils
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.types.mode_t
    libc.src.__support.common
    libc.src.mqueue.types
)

add_entrypoint_object(
  mq_close
  SRCS
    mq_close.cpp
  HDRS
    ../mq_close.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    libc.src.__support.common
    libc.src.mqueue.types
)

add_entrypoint_object(
  mq_unlink
  SRCS
    mq_unlink.cpp
  HDRS
    ../mq_unlink.h
  DEPENDS
    libc.hdr.errno_macros
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    libc.src.__support.common
    libc.src.mqueue.types
)

add_entrypoint_object(
  mq_getattr
  SRCS
    mq_getattr.cpp
  HDRS
    ../mq_getattr.h
  DEPENDS
    .mqueue_utils
    libc.src.__support.common
    libc.src.mqueue.types
)

add_entrypoint_object(
  mq_setattr
  SRCS
    mq_setattr.cpp
  HDRS
    ../mq_setattr.h
  DEPENDS
    .mqueue_utils
    libc.src.__support.common
    libc.src.mqueue.types
)

add_entrypoint_object(
  mq_timedsend
  SRCS
    ${mq_timedsend_source}
  HDRS
    ../mq_timedsend.h
  DEPENDS
    .mqueue_timespec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    libc.hdr.types.size_t
    libc.hdr.types.struct_timespec
    libc.src.__support.common
    libc.src.mqueue.types
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  mq_timedreceive
  SRCS
    ${mq_timedreceive_source}
  HDRS
    ../mq_timedreceive.h
  DEPENDS
    .mqueue_timespec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.macros.sanitizer
    libc.src.errno.errno
    libc.hdr.types.size_t
    libc.hdr.types.ssize_t
    libc.hdr.types.struct_timespec
    libc.src.__support.common
    libc.src.mqueue.types
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

# The untimed wrappers inherit the cancellation provider through their timed
# entrypoints, but their own frames also need to support forced unwinding.
add_entrypoint_object(
  mq_send
  SRCS
    mq_send.cpp
  HDRS
    ../mq_send.h
  DEPENDS
    .mq_timedsend
    libc.hdr.types.size_t
    libc.src.__support.common
    libc.src.mqueue.types
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  mq_receive
  SRCS
    mq_receive.cpp
  HDRS
    ../mq_receive.h
  DEPENDS
    .mq_timedreceive
    libc.hdr.types.size_t
    libc.hdr.types.ssize_t
    libc.src.__support.common
    libc.src.mqueue.types
  COMPILE_OPTIONS ${mmix_cancellation_options}
)
