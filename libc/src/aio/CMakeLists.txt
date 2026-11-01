add_header_library(
  types
  HDRS types.h
  DEPENDS libc.include.llvm-libc-types.struct_aiocb
    libc.include.llvm-libc-types.ssize_t
)

# The provider is target-owned; absent backends do not get success stubs.
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND LLVM_LIBC_FULL_BUILD AND
   LIBC_MMIX_BUILD_DEFERRED_CANCELLATION AND LIBC_MMIX_BUILD_PTHREAD_SCHEDULING)
  add_subdirectory(linux/mmix)
  foreach(name aio_read aio_write aio_fsync aio_error aio_return aio_cancel
      aio_suspend lio_listio)
    add_entrypoint_object(
      ${name}
      ALIAS
      DEPENDS .linux.mmix.${name}
    )
  endforeach()
endif()
