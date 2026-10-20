if(NOT TARGET libc.src.__support.File.file OR NOT TARGET libc.src.__support.File.platform_file)
  return()
endif()

add_object_library(
  lookup
  SRCS shadow_lookup.cpp
  HDRS lookup.h
  DEPENDS
    libc.hdr.types.struct_spwd
    libc.hdr.types.size_t
    libc.src.__support.CPP.limits
    libc.src.__support.CPP.string_view
    libc.src.__support.File.account_database
    libc.src.__support.File.platform_file
    libc.src.pwd.field_tokenizer
)

add_entrypoint_object(
  getspnam_r
  SRCS getspnam_r.cpp
  HDRS getspnam_r.h
  DEPENDS .lookup libc.src.__support.common
)

add_entrypoint_object(
  getspnam
  SRCS getspnam.cpp
  HDRS getspnam.h
  DEPENDS .lookup .getspnam_r libc.src.__support.common
)
