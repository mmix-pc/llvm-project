add_header_library(
  spawn
  HDRS spawn.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.sys_mman_macros
    libc.include.spawn
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
)

set(spawn_source posix_spawn.cpp)
set(spawn_options)
set(spawn_extra_headers)
set(spawn_signal_deps libc.src.signal.linux.signal_utils)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(spawn_source mmix/posix_spawn.cpp)
  set(spawn_signal_deps
    libc.src.__support.threads.linux.mmix.credentials_h
    libc.src.__support.threads.linux.mmix.process_operation_h
    libc.hdr.errno_macros
    libc.hdr.types.struct_sched_param
    libc.src.signal.linux.mmix.private_signals
    libc.src.signal.linux.mmix.rt_sigaction
    libc.src.__support.OSUtil.linux.syscall_wrappers.rt_sigprocmask
    libc.src.__support.OSUtil.linux.syscall_wrappers.wait4)
  if(LIBC_MMIX_BUILD_COORDINATED_FORK)
    foreach(header asm/spawn.h asm/unistd.h)
      get_filename_component(root "${CMAKE_SYSROOT}" REALPATH)
      get_filename_component(path "${CMAKE_SYSROOT}${LIBC_KERNEL_HEADERS}/${header}" REALPATH)
      string(FIND "${path}" "${root}/" inside)
      if(NOT inside EQUAL 0 OR NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
        message(FATAL_ERROR "MMIX coordinated spawn requires in-sysroot ${header}")
      endif()
    endforeach()
    list(APPEND spawn_source mmix/coordinated_spawn.cpp
      mmix/spawn_event.cpp mmix/spawn_event.S)
    list(APPEND spawn_extra_headers mmix/coordinated_spawn.h mmix/spawn_event.h)
    list(APPEND spawn_signal_deps
      libc.src.__support.threads.linux.mmix.fork_runtime)
    list(APPEND spawn_options -DLIBC_MMIX_SPAWN_COORDINATED
      -DLIBC_MMIX_SPAWN_EVENT -fno-lto)
    if(LIBC_MMIX_BUILD_UNWIND_ROOTS)
      list(APPEND spawn_options -DLIBC_MMIX_UNWIND_ROOTS)
      list(APPEND spawn_signal_deps
        libc.src.__support.threads.linux.mmix.unwind_root
        libc.src.__support.threads.linux.mmix.thread_finish
        libc.src.__support.threads.linux.mmix.thread_create)
    endif()
  endif()
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    list(APPEND spawn_options -DLIBC_MMIX_CANCELLATION_WINDOWS)
  endif()
endif()
add_entrypoint_object(
  posix_spawn
  SRCS
    ${spawn_source}
  HDRS
    ../posix_spawn.h
    ${spawn_extra_headers}
  DEPENDS
    libc.hdr.types.mode_t
    libc.hdr.fcntl_macros
    libc.hdr.signal_macros
    libc.include.spawn
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.dup2
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.spawn.file_actions
    .spawn
    ${spawn_signal_deps}
  COMPILE_OPTIONS ${spawn_options}
)

add_entrypoint_object(
  posix_spawnp
  SRCS posix_spawnp.cpp
  HDRS ../posix_spawnp.h
  DEPENDS
    .spawn
    .posix_spawn
    libc.src.__support.CPP.stringstream
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.hdr.func.free
    libc.hdr.func.malloc
    libc.src.stdlib.getenv
)
