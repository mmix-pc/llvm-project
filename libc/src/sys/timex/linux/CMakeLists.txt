if(LIBC_TARGET_ARCHITECTURE MATCHES "^(mmix|x86_64)$")
  add_entrypoint_object(
    adjtimex
    SRCS adjtimex.cpp
    HDRS ../adjtimex.h
    DEPENDS
      libc.hdr.types.struct_timex
      libc.include.sys_syscall
      libc.src.__support.OSUtil.osutil
      libc.src.errno.errno
  )
endif()
