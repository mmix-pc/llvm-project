foreach(name wait waitpid wait4)
  set(${name}_source ${name}.cpp)
endforeach()
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  include(${LIBC_SOURCE_DIR}/config/linux/mmix/CancellationPoints.cmake)
  mmix_select_cancellation_points(wait waitpid wait4)
endif()

add_entrypoint_object(
  waitid
  SRCS waitid.cpp
  HDRS ../waitid.h
  DEPENDS
    libc.hdr.types.id_t
    libc.hdr.types.idtype_t
    libc.hdr.types.siginfo_t
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.errno.errno
)

add_entrypoint_object(
  wait
  SRCS
    ${wait_source}
  HDRS
    ../wait.h
  DEPENDS
    libc.hdr.types.pid_t
    libc.hdr.types.struct_rusage
    libc.src.__support.OSUtil.linux.syscall_wrappers.wait4
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  wait4
  SRCS
    ${wait4_source}
  HDRS
    ../wait4.h
  DEPENDS
    libc.hdr.types.pid_t
    libc.hdr.types.struct_rusage
    libc.src.__support.OSUtil.linux.syscall_wrappers.wait4
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  waitpid
  SRCS
    ${waitpid_source}
  HDRS
    ../waitpid.h
  DEPENDS
    libc.hdr.types.pid_t
    libc.hdr.types.struct_rusage
    libc.src.__support.OSUtil.linux.syscall_wrappers.wait4
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)
