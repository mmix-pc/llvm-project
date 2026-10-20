if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${LIBC_TARGET_OS})
endif()

foreach(name getxattr setxattr listxattr removexattr lgetxattr lsetxattr llistxattr lremovexattr fgetxattr fsetxattr flistxattr fremovexattr)
  add_entrypoint_object(
    ${name}
    ALIAS
    DEPENDS .${LIBC_TARGET_OS}.${name}
  )
endforeach()
