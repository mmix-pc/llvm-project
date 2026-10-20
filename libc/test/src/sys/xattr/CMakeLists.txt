add_custom_target(libc_sys_xattr_unittests)

set(xattr_deps)
foreach(name getxattr setxattr listxattr removexattr
    lgetxattr lsetxattr llistxattr lremovexattr
    fgetxattr fsetxattr flistxattr fremovexattr)
  list(APPEND xattr_deps libc.src.sys.xattr.${name})
endforeach()
add_libc_test(
  xattr_test
  SUITE libc_sys_xattr_unittests
  SRCS xattr_test.cpp
  DEPENDS
    ${xattr_deps}
    libc.hdr.fcntl_macros
    libc.hdr.sys_xattr_macros
    libc.src.errno.errno
    libc.src.fcntl.open
    libc.src.unistd.close
    libc.src.unistd.symlink
    libc.src.unistd.unlink
)
