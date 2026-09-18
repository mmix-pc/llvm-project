if(NOT TARGET libc.src.__support.File.file OR NOT TARGET libc.src.__support.File.platform_file)
  return()
endif()

add_object_library(
  lookup
  SRCS group_lookup.cpp
  HDRS lookup.h
  DEPENDS
    libc.hdr.types.struct_group
    libc.hdr.types.size_t
    libc.hdr.stdint_proxy
    libc.src.__support.CPP.new
    libc.src.__support.File.account_database
    libc.src.__support.File.platform_file
    libc.src.__support.ctype_utils
    libc.src.__support.str_to_integer
    libc.src.pwd.field_tokenizer
)
foreach(name getgrnam_r getgrgid_r)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .lookup libc.src.__support.common
  )
endforeach()
foreach(name getgrnam getgrgid)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .lookup .${name}_r libc.src.__support.common
  )
endforeach()
