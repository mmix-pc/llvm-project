option(LIBC_MMIX_BUILD_EXTENDED_ATTRIBUTES
  "Build Linux extended-attribute interfaces" OFF)
if(NOT LIBC_MMIX_BUILD_EXTENDED_ATTRIBUTES)
  return()
endif()

foreach(name getxattr setxattr listxattr removexattr
    lgetxattr lsetxattr llistxattr lremovexattr
    fgetxattr fsetxattr flistxattr fremovexattr)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.sys.xattr.${name})
endforeach()
