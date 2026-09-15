option(LIBC_MMIX_BUILD_VM "Build Linux vm providers" OFF)
if(NOT LIBC_MMIX_BUILD_VM)
  return()
endif()

set(MMIX_VM_ENTRYPOINTS
  libc.src.sys.mman.mmap
  libc.src.sys.mman.munmap
  libc.src.sys.mman.mprotect
  libc.src.unistd.sysconf
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_VM_ENTRYPOINTS})
