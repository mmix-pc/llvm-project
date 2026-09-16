add_object_library(
  signal_trampoline
  SRCS signal_trampoline.S
  HDRS signal_trampoline.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.setjmp.mmix.jump_fail
)

add_header_library(
  kernel_sigaction
  HDRS
    kernel_sigaction.h
  DEPENDS
    libc.hdr.signal_macros
    libc.hdr.types.struct_sigaction
    libc.src.__support.common
    libc.src.__support.macros.config
)

add_header_library(
  rt_sigaction
  HDRS
    rt_sigaction.h
  DEPENDS
    .kernel_sigaction
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.error_or
)
