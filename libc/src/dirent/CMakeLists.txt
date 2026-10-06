set(closedir_source closedir.cpp)
set(closedir_dependency)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND
   LIBC_MMIX_BUILD_CANCELLATION_RUNTIME_LOCKS)
  set(closedir_source linux/mmix/cancellation_closedir.cpp)
  set(closedir_dependency libc.src.__support.threads.linux.mmix.cancellation)
endif()

add_entrypoint_object(
  opendir
  SRCS
    opendir.cpp
  HDRS
    opendir.h
  DEPENDS
    libc.include.dirent
    libc.src.__support.File.dir
    libc.src.__support.File.platform_dir
    libc.src.errno.errno
)

add_entrypoint_object(
  dirfd
  SRCS
    dirfd.cpp
  HDRS
    dirfd.h
  DEPENDS
    libc.include.dirent
    libc.src.__support.File.dir
    libc.src.__support.File.platform_dir
)

add_entrypoint_object(
  closedir
  SRCS
    ${closedir_source}
  HDRS
    closedir.h
  DEPENDS
    libc.include.dirent
    libc.src.__support.File.dir
    libc.src.__support.File.platform_dir
    libc.src.errno.errno
    ${closedir_dependency}
)

add_entrypoint_object(
  readdir
  SRCS
    readdir.cpp
  HDRS
    readdir.h
  DEPENDS
    libc.include.dirent
    libc.src.__support.File.dir
    libc.src.__support.File.platform_dir
    libc.src.errno.errno
)

add_entrypoint_object(
  fdopendir
  SRCS
    fdopendir.cpp
  HDRS
    fdopendir.h
  DEPENDS
    libc.src.__support.common
    libc.src.__support.macros.config
    libc.hdr.types.DIR
    libc.src.__support.File.dir
    libc.src.errno.errno
)
