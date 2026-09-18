if(NOT TARGET libc.src.__support.File.platform_file)
  return()
endif()
add_object_library(
  mntent_utils
  SRCS mntent_utils.cpp
  HDRS mntent_utils.h
  DEPENDS
    libc.hdr.types.struct_mntent
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
    libc.src.__support.ctype_utils
    libc.src.__support.str_to_integer
    libc.src.__support.libc_errno
)
foreach(name setmntent getmntent getmntent_r endmntent)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS
      .mntent_utils
      libc.hdr.types.FILE
      libc.hdr.func.realloc
      libc.src.__support.common
  )
endforeach()
add_entrypoint_object(
  hasmntopt
  SRCS hasmntopt.cpp
  HDRS hasmntopt.h
  DEPENDS
    libc.hdr.types.FILE
    libc.hdr.types.struct_mntent
    libc.src.__support.CPP.string_view
    libc.src.__support.common
)
