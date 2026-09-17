option(LIBC_MMIX_BUILD_FILESYSTEM "Build Linux filesystem providers" OFF)
if(NOT LIBC_MMIX_BUILD_FILESYSTEM)
  return()
endif()

set(MMIX_FILESYSTEM_ENTRYPOINTS
  libc.src.sys.stat.umask
  libc.src.sys.stat.stat
  libc.src.sys.stat.fstat
  libc.src.sys.stat.lstat
  libc.src.sys.stat.mkdir
  libc.src.sys.stat.chmod
  libc.src.sys.stat.fchmod
  libc.src.sys.mman.msync
  libc.src.sys.mman.posix_madvise
  libc.src.unistd.usleep
  libc.src.unistd.fchown
  libc.src.unistd.access
  libc.src.unistd.faccessat
  libc.src.unistd.chdir
  libc.src.unistd.getcwd
  libc.src.unistd.readlink
  libc.src.unistd.readlinkat
  libc.src.unistd.unlink
  libc.src.unistd.unlinkat
  libc.src.unistd.rmdir
  libc.src.unistd.link
  libc.src.unistd.symlink
  libc.src.unistd.ftruncate
  libc.src.unistd.truncate
  libc.src.stdio.rename
  libc.src.stdio.remove
  libc.src.stdlib.realpath
  libc.src.dirent.opendir
  libc.src.dirent.fdopendir
  libc.src.dirent.readdir
  libc.src.dirent.closedir
  libc.src.dirent.dirfd
  libc.src.sys.statfs.statfs
  libc.src.sys.statfs.fstatfs
  libc.src.unistd.getuid
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_FILESYSTEM_ENTRYPOINTS})
