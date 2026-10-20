foreach(name getxattr setxattr listxattr removexattr lgetxattr lsetxattr llistxattr lremovexattr fgetxattr fsetxattr flistxattr fremovexattr)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ../${name}.h
    DEPENDS
      libc.hdr.types.size_t
      libc.hdr.types.ssize_t
      libc.include.sys_syscall
      libc.src.__support.OSUtil.osutil
      libc.src.__support.common
      libc.src.errno.errno
  )
endforeach()
