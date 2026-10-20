# TODO: migrate to proxy headers
set(stream_io_source file_io.cpp)
set(directory_io_source dir_io.cpp)
set(stream_io_dependency)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
   LIBC_MMIX_BUILD_CANCELLATION_RUNTIME_LOCKS)
  set(stream_io_source mmix/cancellation_file_io.cpp)
  set(directory_io_source mmix/cancellation_dir_io.cpp)
  set(stream_io_dependency libc.src.__support.threads.linux.mmix.cancellation_point)
endif()
add_object_library(
  file
  SRCS
    file.cpp
    ${stream_io_source}
  HDRS
    file.h
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.stdio_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.FILE
    libc.hdr.types.off_t
    libc.hdr.types.mode_t
    libc.src.__support.CPP.new
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fcntl
    libc.src.__support.OSUtil.linux.syscall_wrappers.lseek
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.read
    libc.src.__support.OSUtil.linux.syscall_wrappers.write
    libc.src.__support.error_or
    libc.src.__support.File.file
    libc.src.errno.errno
    ${stream_io_dependency}
)

add_object_library(
  linux_dir
  SRCS
    dir.cpp
    ${directory_io_source}
  DEPENDS
    libc.src.__support.common
    libc.src.__support.macros.config
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.src.__support.OSUtil.linux.stat.kernel_statx_types
    libc.src.__support.OSUtil.osutil
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.fcntl
    libc.src.__support.OSUtil.linux.syscall_wrappers.statx
    libc.src.__support.OSUtil.linux.syscall_wrappers.lseek
    libc.src.__support.error_or
    libc.src.errno.errno
    libc.src.__support.File.dir
    ${stream_io_dependency}
)
