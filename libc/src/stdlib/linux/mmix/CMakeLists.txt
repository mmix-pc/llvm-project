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

foreach(entrypoint malloc free calloc realloc)
  add_entrypoint_object(${entrypoint}
    SRCS ${entrypoint}.cpp
    HDRS ../../${entrypoint}.h
    DEPENDS
      .allocator
      libc.src.__support.common
      libc.src.string.memory_utils.inline_memset
      libc.src.__support.libc_errno
      libc.src.errno.errno)
endforeach()
