add_header_library(
  public_result
  HDRS public_result.h
  DEPENDS libc.hdr.errno_macros libc.src.errno.errno
    libc.src.__support.macros.attributes
)

foreach(name aio_read aio_write aio_fsync aio_error aio_return aio_cancel
    aio_suspend lio_listio)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ../../${name}.h
    DEPENDS .public_result libc.src.aio.types
      libc.src.__support.common
      libc.src.__support.threads.linux.mmix.aio_service
      libc.src.__support.threads.linux.mmix.cancellation
      libc.hdr.types.struct_timespec
    COMPILE_OPTIONS -fno-lto -fexceptions -funwind-tables
  )
endforeach()
