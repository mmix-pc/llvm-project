option(LIBC_MMIX_BUILD_DESCRIPTORS "Build Linux descriptors providers" OFF)
if(NOT LIBC_MMIX_BUILD_DESCRIPTORS)
  return()
endif()

set(MMIX_DESCRIPTORS_ENTRYPOINTS
  libc.src.fcntl.creat
  libc.src.fcntl.open
  libc.src.fcntl.openat
  libc.src.fcntl.fcntl
  libc.src.unistd.close
  libc.src.unistd.read
  libc.src.unistd.write
  libc.src.unistd.lseek
  libc.src.unistd.pread
  libc.src.unistd.pwrite
  libc.src.unistd.dup
  libc.src.unistd.dup2
  libc.src.unistd.dup3
  libc.src.unistd.pipe
  libc.src.unistd.pipe2
  libc.src.unistd.fsync
  libc.src.unistd.isatty
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_DESCRIPTORS_ENTRYPOINTS})
