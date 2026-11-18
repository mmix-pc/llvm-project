if(NOT LIBC_TARGET_OS STREQUAL "linux")
  return()
endif()
foreach(name capget capset)
  add_entrypoint_object(${name} SRCS ${name}.cpp HDRS ${name}.h
    DEPENDS libc.include.sys_syscall libc.src.__support.OSUtil.osutil
      libc.src.errno.errno)
endforeach()
