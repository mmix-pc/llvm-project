foreach(name open openat)
  set(${name}_source ${name}.cpp)
endforeach()
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  include(${LIBC_SOURCE_DIR}/config/linux/mmix/CancellationPoints.cmake)
  mmix_select_cancellation_points(open openat)
endif()

add_entrypoint_object(
  creat
  SRCS
    creat.cpp
  HDRS
    ../creat.h
  DEPENDS
    libc.hdr.types.mode_t
    libc.hdr.fcntl_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_header_library(
  fcntl_args
  HDRS fcntl_args.h
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.stdint_proxy
    libc.hdr.types.struct_f_owner_ex
    libc.hdr.types.struct_flock
    libc.hdr.types.struct_flock64
    libc.src.__support.macros.attributes
    libc.src.__support.macros.config
)

add_entrypoint_object(
  fcntl
  SRCS
    fcntl.cpp
  HDRS
    ../fcntl.h
  DEPENDS
    .fcntl_args
    libc.hdr.stdint_proxy
    libc.hdr.fcntl_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.fcntl
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  open
  SRCS
    ${open_source}
  HDRS
    ../open.h
  DEPENDS
    libc.hdr.types.mode_t
    libc.hdr.fcntl_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  openat
  SRCS
    ${openat_source}
  HDRS
    ../openat.h
  DEPENDS
    libc.hdr.types.mode_t
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)
