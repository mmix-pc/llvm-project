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

foreach(name rewinddir seekdir telldir readdir_r)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS
      libc.hdr.types.DIR
      libc.hdr.types.struct_dirent
      libc.src.__support.File.dir
      libc.src.__support.File.platform_dir
      libc.src.errno.errno
  )
endforeach()

add_entrypoint_object(
  alphasort
  SRCS alphasort.cpp
  HDRS alphasort.h
  DEPENDS libc.hdr.types.struct_dirent libc.src.string.strcoll
)
add_entrypoint_object(
  versionsort
  SRCS versionsort.cpp
  HDRS versionsort.h
  DEPENDS libc.hdr.types.struct_dirent libc.src.string.strverscmp
)

add_header_library(
  scandir_utils
  HDRS scandir_utils.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.func.free
    libc.hdr.func.malloc
    libc.hdr.func.realloc
    libc.src.__support.CPP.limits
    libc.src.__support.File.dir
    libc.src.__support.File.platform_dir
    libc.src.__support.libc_errno
    libc.src.fcntl.openat
    libc.src.stdlib.qsort_util
    libc.src.string.memory_utils.inline_memcpy
)
foreach(name scandir scandirat)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .scandir_utils
  )
endforeach()

if(LIBC_TARGET_OS_IS_LINUX)
  add_entrypoint_object(
    posix_getdents
    SRCS linux/posix_getdents.cpp
    HDRS posix_getdents.h
    DEPENDS
      libc.include.dirent
      libc.hdr.types.size_t
      libc.hdr.types.ssize_t
      libc.src.__support.CPP.limits
      libc.src.__support.File.platform_dir
      libc.src.errno.errno
  )
endif()
