foreach(name msgsnd msgrcv)
  set(${name}_source ${name}.cpp)
endforeach()
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  include(${LIBC_SOURCE_DIR}/config/linux/mmix/CancellationPoints.cmake)
  mmix_select_cancellation_points(msgsnd msgrcv)
endif()

add_entrypoint_object(
  msgget
  SRCS
    msgget.cpp
  HDRS
    ../msgget.h
  DEPENDS
    libc.hdr.types.key_t
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.errno.errno
)

add_entrypoint_object(
  msgsnd
  SRCS
    ${msgsnd_source}
  HDRS
    ../msgsnd.h
  DEPENDS
    libc.hdr.types.size_t
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  msgrcv
  SRCS
    ${msgrcv_source}
  HDRS
    ../msgrcv.h
  DEPENDS
    libc.hdr.types.size_t
    libc.hdr.types.ssize_t
    libc.src.__support.macros.sanitizer
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  msgctl
  SRCS
    msgctl.cpp
  HDRS
    ../msgctl.h
  DEPENDS
    libc.hdr.types.struct_msqid_ds
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.errno.errno
)
