option(LIBC_MMIX_BUILD_VM "Build Linux vm providers" OFF)
if(NOT LIBC_MMIX_BUILD_VM)
  return()
endif()

set(MMIX_VM_ENTRYPOINTS
  libc.src.sys.mman.madvise
  libc.src.sys.mman.memfd_create
  libc.src.sys.mman.mincore
  libc.src.sys.mman.mlock
  libc.src.sys.mman.mlock2
  libc.src.sys.mman.mlockall
  libc.src.sys.mman.mmap
  libc.src.sys.mman.mremap
  libc.src.sys.mman.munlock
  libc.src.sys.mman.munlockall
  libc.src.sys.mman.munmap
  libc.src.sys.mman.mprotect
  libc.src.sys.mman.remap_file_pages
  libc.src.sys.mman.shm_open
  libc.src.sys.mman.shm_unlink
  libc.src.unistd.sysconf
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_VM_ENTRYPOINTS})
