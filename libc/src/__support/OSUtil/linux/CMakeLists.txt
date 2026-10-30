if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_ARCHITECTURE})
  return()
endif()

add_subdirectory(${LIBC_TARGET_ARCHITECTURE})
if(NOT TARGET libc.src.__support.OSUtil.linux.${LIBC_TARGET_ARCHITECTURE}.linux_${LIBC_TARGET_ARCHITECTURE}_util)
  return()
endif()

add_subdirectory(syscall_wrappers)
add_subdirectory(stat)

add_header_library(
  notification_channels
  HDRS notification_channels.h
  DEPENDS
    .notification_transport libc.src.__support.OSUtil.osutil
    libc.hdr.errno_macros libc.hdr.signal_macros libc.hdr.stdint_proxy
    libc.hdr.sys_socket_macros libc.hdr.types.struct_iovec
    libc.hdr.types.struct_msghdr libc.src.__support.error_or
    libc.src.__support.macros.config libc.include.sys_syscall
)

add_header_library(
  notification_transport
  HDRS
    notification_transport.h
  DEPENDS
    libc.hdr.signal_macros
    libc.hdr.stdint_proxy
    libc.hdr.sys_socket_macros
    libc.src.__support.macros.attributes
    libc.src.__support.macros.config
)

add_header_library(
  kernel_sigevent
  HDRS
    kernel_sigevent.h
  DEPENDS
    libc.hdr.signal_macros
    libc.hdr.types.pid_t
    libc.hdr.types.struct_sigevent
    libc.src.__support.common
)

add_header_library(
  aio_engine
  HDRS aio_engine.h
  DEPENDS
    .aio_completion_queue .aio_submission_resources .stat.kernel_statx_types
    libc.src.__support.CPP.new
    libc.src.__support.threads.aio_request
    libc.src.__support.threads.aio_file_dependencies
)

add_header_library(
  aio_wait_event
  HDRS aio_wait_event.h
  DEPENDS libc.hdr.errno_macros libc.hdr.stdint_proxy
    libc.hdr.types.struct_timespec libc.src.__support.CPP.atomic
    libc.src.__support.CPP.type_traits
    libc.src.__support.OSUtil.osutil
    libc.src.__support.threads.linux.futex_utils
)

add_header_library(
  aio_completion_queue
  HDRS aio_completion_queue.h
  DEPENDS
    .io_uring
    libc.hdr.errno_macros
    libc.hdr.stdint_proxy
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.error_or
    libc.src.__support.macros.attributes
    libc.src.__support.macros.config
    libc.src.__support.threads.aio_completion_budget
)

add_header_library(
  aio_submission_resources
  HDRS aio_submission_resources.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.error_or
    libc.src.__support.macros.attributes
    libc.src.__support.macros.config
)

add_header_library(
  io_uring
  HDRS io_uring.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.stdint_proxy
    libc.hdr.sys_mman_macros
    libc.hdr.types.off_t
    libc.include.sys_syscall
    libc.src.__support.CPP.atomic
    libc.src.__support.OSUtil.osutil
    libc.src.__support.OSUtil.linux.syscall_wrappers.mmap
    libc.src.__support.error_or
    libc.src.__support.macros.config
)

set(secure_execution_source secure_execution.cpp)
set(secure_execution_deps .auxv)
if(LIBC_TARGET_ARCHITECTURE_IS_MMIX)
  set(secure_execution_source mmix/secure_execution.cpp)
  set(secure_execution_deps .mmix.startup_security)
endif()
add_object_library(
  secure_execution
  SRCS ${secure_execution_source}
  HDRS secure_execution.h
  DEPENDS ${secure_execution_deps} libc.src.__support.macros.config
)

add_header_library(
  startup_fds
  HDRS startup_fds.h
  DEPENDS libc.hdr.errno_macros libc.hdr.fcntl_macros libc.hdr.sys_stat_macros
    libc.include.sys_syscall libc.src.__support.macros.config
)

add_header_library(
  startup_security
  HDRS startup_security.h
  DEPENDS libc.hdr.stdint_proxy libc.hdr.sys_auxv_macros
    libc.src.__support.macros.config
)

add_header_library(
  loader_services
  HDRS loader_services.h
  DEPENDS libc.hdr.stdint_proxy libc.src.__support.macros.config
)

add_header_library(
  fork_recovery
  HDRS fork_recovery.h
  DEPENDS libc.src.__support.macros.config
)

add_object_library(
  linux_util
  SRCS
    exit.cpp
  HDRS
    io.h
    syscall.h
  DEPENDS
    .${LIBC_TARGET_ARCHITECTURE}.linux_${LIBC_TARGET_ARCHITECTURE}_util
    libc.src.__support.common
    libc.src.__support.CPP.string_view
    libc.include.sys_syscall
)

add_header_library(
  auxv
  HDRS
    auxv.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.sys_auxv_macros
    libc.hdr.sys_mman_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.mmap
    libc.src.__support.OSUtil.linux.syscall_wrappers.munmap
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.read
    libc.src.__support.OSUtil.osutil
    libc.src.__support.common
    libc.src.__support.CPP.optional
    libc.src.__support.threads.callonce
)

add_header_library(
  network_syscall_policy
  HDRS
    network_syscall_policy.h
  DEPENDS
    libc.hdr.types.socklen_t
    libc.hdr.types.ssize_t
    libc.hdr.types.struct_sockaddr
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.recvfrom
    libc.src.__support.OSUtil.linux.syscall_wrappers.sendto
    libc.src.__support.OSUtil.linux.syscall_wrappers.socket
    libc.src.__support.common
    libc.src.__support.error_or
)

add_header_library(
  path
  HDRS
    path.h
  DEPENDS
    libc.src.__support.common
    libc.src.__support.CPP.string_view
)

add_header_library(
  sysinfo
  HDRS
    sysinfo.h
  DEPENDS
    libc.hdr.errno_macros
    libc.src.__support.CPP.array
    libc.src.__support.CPP.bit
    libc.src.__support.CPP.optional
    libc.src.__support.ctype_utils
    libc.src.__support.macros.config
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.read
    libc.src.__support.OSUtil.linux.syscall_wrappers.sched_getaffinity
)

add_header_library(
  vdso_sym
  HDRS
    vdso_sym.h
  DEPENDS
    libc.src.__support.common
)

if(NOT TARGET libc.src.__support.OSUtil.linux.${LIBC_TARGET_ARCHITECTURE}.vdso)
  return()
endif()

add_object_library(
  vdso
  HDRS
    vdso.h
  SRCS
    vdso.cpp
  DEPENDS
    .${LIBC_TARGET_ARCHITECTURE}.vdso
    libc.src.__support.CPP.array
    libc.src.__support.CPP.optional
    libc.src.__support.CPP.string_view
    libc.src.__support.threads.callonce
    libc.src.__support.threads.linux.futex_word_type
    libc.src.__support.OSUtil.linux.auxv
    libc.hdr.elf_proxy
    libc.hdr.link_macros
    libc.hdr.sys_auxv_macros
    libc.hdr.types.clockid_t
    libc.hdr.types.struct_timespec
    libc.hdr.types.struct_timeval
    libc.hdr.types.time_t
)
