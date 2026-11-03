add_header_library(
  abort_utils
  HDRS abort_utils.h
  DEPENDS
    libc.src.__support.OSUtil.osutil
    libc.src.__support.OSUtil.linux.syscall_wrappers.raise
    libc.src.signal.linux.mmix.rt_sigaction
)

add_entrypoint_object(
  posix_openpt
  SRCS posix_openpt.cpp
  HDRS ../../posix_openpt.h
  DEPENDS libc.hdr.fcntl_macros libc.src.fcntl.open libc.src.errno.errno
  COMPILE_OPTIONS -fexceptions -funwind-tables
)

set(pty_cancel_dependency)
set(pty_cancel_options)
if(TARGET libc.src.__support.threads.linux.mmix.cancellation)
  set(pty_cancel_dependency libc.src.__support.threads.linux.mmix.cancellation)
  set(pty_cancel_options -DLIBC_MMIX_PTY_CANCELLATION -fexceptions -funwind-tables)
endif()
add_entrypoint_object(
  grantpt
  SRCS grantpt.cpp
  HDRS ../../grantpt.h
  DEPENDS
    libc.hdr.fcntl_macros libc.hdr.sys_ioctl_macros libc.hdr.sys_stat_macros
    libc.hdr.types.gid_t libc.hdr.types.mode_t libc.hdr.types.uid_t
    libc.include.sys_syscall
    libc.src.__support.OSUtil.linux.stat.kernel_statx_types
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fchmodat
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.__support.OSUtil.linux.syscall_wrappers.statx
    libc.src.errno.errno
    ${pty_cancel_dependency}
  COMPILE_OPTIONS ${pty_cancel_options}
)
add_entrypoint_object(
  unlockpt
  SRCS unlockpt.cpp
  HDRS ../../unlockpt.h
  DEPENDS
    libc.hdr.sys_ioctl_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)

add_entrypoint_object(
  ptsname_r
  SRCS ptsname_r.cpp
  HDRS ../../ptsname_r.h
  DEPENDS
    libc.hdr.errno_macros libc.hdr.fcntl_macros libc.hdr.limits_macros
    libc.hdr.sys_ioctl_macros libc.hdr.sys_stat_macros libc.hdr.types.size_t
    libc.include.sys_syscall
    libc.src.__support.common
    libc.src.__support.CPP.stringstream
    libc.src.__support.OSUtil.linux.stat.kernel_statx_types
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.__support.OSUtil.linux.syscall_wrappers.readlinkat
    libc.src.__support.OSUtil.linux.syscall_wrappers.statx
    libc.src.string.memory_utils.inline_memcpy
    ${pty_cancel_dependency}
  COMPILE_OPTIONS ${pty_cancel_options}
)
add_entrypoint_object(
  ptsname
  SRCS ptsname.cpp
  HDRS ../../ptsname.h
  DEPENDS libc.hdr.limits_macros libc.src.__support.common
    libc.src.errno.errno .ptsname_r
  COMPILE_OPTIONS ${pty_cancel_options}
)

add_entrypoint_object(
  secure_getenv
  SRCS secure_getenv.cpp
  HDRS ../../secure_getenv.h
  DEPENDS libc.src.stdlib.getenv
    libc.src.__support.OSUtil.linux.mmix.startup_security
    libc.src.__support.common
)

add_object_library(
  allocator
  SRCS allocator.cpp
  HDRS allocator.h
  DEPENDS
    libc.src.__support.OSUtil.linux.mmix.heap
    libc.src.__support.CPP.limits
    libc.src.__support.CPP.mutex
    libc.src.__support.CPP.new
    libc.src.__support.block
    libc.src.__support.freelist
    libc.src.__support.freestore
    libc.src.__support.freetrie
    libc.src.__support.libc_assert
    libc.src.__support.threads.raw_mutex
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.memory_utils.inline_memset
)

foreach(entrypoint malloc free calloc realloc aligned_alloc posix_memalign)
  if(entrypoint STREQUAL "posix_memalign")
    set(header posix_memalign.h)
  else()
    set(header ../../${entrypoint}.h)
  endif()
  add_entrypoint_object(${entrypoint}
    SRCS ${entrypoint}.cpp
    HDRS ${header}
    DEPENDS
      .allocator
      libc.src.__support.common
      libc.src.string.memory_utils.inline_memset
      libc.src.__support.libc_errno
      libc.src.errno.errno)
endforeach()
