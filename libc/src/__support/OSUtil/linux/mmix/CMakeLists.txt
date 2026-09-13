add_object_library(
  syscall_asm
  SRCS
    syscall.S
  HDRS
    syscall.h
)

# Keep the syscall interface available to independent consumers.
add_header_library(
  syscall
  HDRS
    syscall.h
  DEPENDS
    .syscall_asm
    libc.src.__support.common
)

add_header_library(
  linux_mmix_util
  HDRS
    syscall.h
  DEPENDS
    .syscall
)
