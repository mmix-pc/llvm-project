set(readv_source readv.cpp)
set(writev_source writev.cpp)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  include(${LIBC_SOURCE_DIR}/config/linux/mmix/CancellationPoints.cmake)
  mmix_select_cancellation_points(readv writev)
endif()

add_entrypoint_object(
  writev
  SRCS
    ${writev_source}
  HDRS
    ../writev.h
  DEPENDS
    libc.hdr.types.ssize_t
    libc.hdr.types.struct_iovec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  readv
  SRCS
    ${readv_source}
  HDRS
    ../readv.h
  DEPENDS
    libc.hdr.types.ssize_t
    libc.hdr.types.struct_iovec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)
