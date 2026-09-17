foreach(name getpwnam_r getpwuid_r)
  add_entrypoint_object(
    ${name}
    SRCS linux/mmix/${name}.cpp
    HDRS ${name}.h linux/mmix/lookup.h
    DEPENDS
      libc.src.__support.common
      libc.src.__support.File.file
      libc.src.__support.File.platform_file
      libc.src.__support.libc_errno
      libc.src.pwd.pwd_utils
  )
endforeach()
