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
