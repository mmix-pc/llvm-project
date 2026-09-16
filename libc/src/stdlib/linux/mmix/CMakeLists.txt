add_header_library(
  abort_utils
  HDRS abort_utils.h
  DEPENDS
    libc.src.__support.OSUtil.osutil
    libc.src.__support.OSUtil.linux.syscall_wrappers.raise
    libc.src.signal.linux.mmix.rt_sigaction
)

add_object_library(
  allocator
  SRCS allocator.cpp
  HDRS allocator.h
  DEPENDS
    libc.src.__support.OSUtil.linux.mmix.heap
    libc.src.__support.CPP.limits
    libc.src.__support.CPP.new
    libc.src.__support.block
    libc.src.__support.freelist
    libc.src.__support.freestore
    libc.src.__support.freetrie
    libc.src.__support.libc_assert
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.memory_utils.inline_memset
)

foreach(entrypoint malloc free calloc realloc aligned_alloc posix_memalign)
  if(entrypoint STREQUAL "posix_memalign")
    set(header posix_memalign.h)
  else()
    set(header ../../${entrypoint}.h)
  endif()
  add_entrypoint_object(${entrypoint}
    SRCS ${entrypoint}.cpp
    HDRS ${header}
    DEPENDS
      .allocator
      libc.src.__support.common
      libc.src.string.memory_utils.inline_memset
      libc.src.__support.libc_errno
      libc.src.errno.errno)
endforeach()
