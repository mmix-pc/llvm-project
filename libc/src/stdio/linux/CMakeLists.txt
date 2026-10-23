add_entrypoint_object(
  ctermid
  SRCS ctermid.cpp
  HDRS ../ctermid.h
  DEPENDS
    libc.hdr.stdio_macros
    libc.src.__support.common
    libc.src.string.memory_utils.inline_memcpy
)

# The spawn providers used by pipe streams are only built with the full libc.
if(LLVM_LIBC_FULL_BUILD)
  set(pipe_process_source pipe_process.cpp)
  set(pipe_fork_source pipe_fork.cpp)
  set(pipe_fork_deps libc.src.__support.threads.fork_callbacks)
  if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
     LIBC_MMIX_BUILD_COORDINATED_FORK)
    set(pipe_fork_source mmix/pipe_fork.cpp)
    set(pipe_fork_deps libc.src.__support.threads.linux.mmix.fork_runtime)
  endif()
  set(pipe_process_deps)
  set(pipe_compile_options)
  if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
     LIBC_MMIX_BUILD_CANCELLATION_RUNTIME_LOCKS)
    set(pipe_process_source mmix/cancellation_pipe_process.cpp)
    set(pipe_process_deps
      libc.hdr.sys_wait_macros
      libc.hdr.types.siginfo_t
      libc.include.pthread
      libc.src.__support.threads.linux.mmix.cancellation
      libc.src.__support.threads.linux.mmix.cancellation_point)
    set(pipe_compile_options -fno-lto -fexceptions -funwind-tables)
  endif()

  add_object_library(
    pipe_file
    SRCS pipe_file.cpp ${pipe_process_source} ${pipe_fork_source}
    HDRS pipe_file.h pipe_process.h pipe_fork.h
    DEPENDS
      libc.hdr.types.pid_t
      libc.src.__support.CPP.mutex
      libc.src.__support.File.file
      libc.src.__support.File.platform_file
      libc.src.__support.OSUtil.linux.syscall_wrappers.close
      libc.src.__support.OSUtil.linux.syscall_wrappers.fcntl
      libc.src.__support.OSUtil.linux.syscall_wrappers.wait4
      libc.src.__support.alloc_checker
      libc.src.__support.threads.raw_mutex
      ${pipe_fork_deps}
      libc.src.errno.errno
      libc.src.spawn.posix_spawn
      libc.src.spawn.posix_spawn_file_actions_addclose
      libc.src.spawn.posix_spawn_file_actions_adddup2
      libc.src.spawn.posix_spawn_file_actions_destroy
      libc.src.unistd.environ
      ${pipe_process_deps}
    COMPILE_OPTIONS ${pipe_compile_options}
  )

  foreach(name popen pclose)
    add_entrypoint_object(
      ${name}
      SRCS ${name}.cpp
      HDRS ../${name}.h
      DEPENDS
        .pipe_file
        libc.hdr.types.FILE
        libc.src.__support.common
      COMPILE_OPTIONS ${pipe_compile_options}
    )
  endforeach()
endif()

add_entrypoint_object(
  remove
  SRCS
    remove.cpp
  HDRS
    ../remove.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.include.unistd
    libc.src.__support.OSUtil.linux.syscall_wrappers.unlinkat
    libc.src.errno.errno
)

add_entrypoint_object(
  rename
  SRCS
    rename.cpp
  HDRS
    ../rename.h
  DEPENDS
    libc.src.__support.OSUtil.linux.syscall_wrappers.rename
    libc.src.errno.errno
)

add_entrypoint_object(
  renameat
  SRCS renameat.cpp
  HDRS ../renameat.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  fdopen
  SRCS
    fdopen.cpp
  HDRS
    ../fdopen.h
  DEPENDS
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
)

add_entrypoint_object(
  stdin
  SRCS
    stdin.cpp
  HDRS
    ../stdin.h
  DEPENDS
    libc.hdr.types.FILE
    libc.src.__support.File.platform_file
)

add_entrypoint_object(
  stdout
  SRCS
    stdout.cpp
  HDRS
    ../stdout.h
  DEPENDS
    libc.hdr.types.FILE
    libc.src.__support.File.platform_file
)

add_entrypoint_object(
  stderr
  SRCS
    stderr.cpp
  HDRS
    ../stderr.h
  DEPENDS
    libc.hdr.types.FILE
    libc.src.__support.File.platform_file
)

add_entrypoint_object(
  tmpfile
  SRCS
    tmpfile.cpp
  HDRS
    ../tmpfile.h
  DEPENDS
    libc.hdr.fcntl_macros
    libc.hdr.types.FILE
    libc.src.__support.common
    libc.src.__support.File.platform_file
    libc.src.__support.libc_errno
    libc.src.__support.macros.config
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
)
# Target-owned resource retirement policy; generic File ownership is retained.
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
   LIBC_MMIX_BUILD_CANCELLATION_RUNTIME_LOCKS)
  add_entrypoint_object(
    fclose
    SRCS mmix/cancellation_fclose.cpp
    HDRS ../fclose.h
    DEPENDS
      libc.src.__support.File.file
      libc.src.__support.File.platform_file
      libc.src.__support.threads.linux.mmix.cancellation
      libc.src.errno.errno
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables
  )
endif()

add_entrypoint_object(
  tmpnam
  SRCS tmpnam.cpp
  HDRS ../tmpnam.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.stdio_macros
    libc.src.__support.CPP.atomic
    libc.src.__support.OSUtil.linux.syscall_wrappers.getrandom
    libc.src.__support.OSUtil.linux.syscall_wrappers.statx
    libc.src.__support.common
    libc.src.__support.libc_errno
)
