if(NOT TARGET libc.src.__support.File.file OR NOT TARGET libc.src.__support.File.platform_file)
  return()
endif()

add_object_library(
  lookup
  SRCS passwd_lookup.cpp
  HDRS lookup.h
  DEPENDS
    .pwd_utils
    libc.src.__support.File.account_database
    libc.src.__support.File.platform_file
)

foreach(name getpwnam_r getpwuid_r)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .lookup libc.src.__support.common
  )
endforeach()

foreach(name getpwnam getpwuid)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS .lookup .${name}_r libc.src.__support.common
  )
endforeach()

add_entrypoint_object(
  getpwent
  SRCS
    getpwent.cpp
  HDRS
    getpwent.h
  DEPENDS
    libc.hdr.types.struct_passwd
    libc.src.errno.errno
    libc.src.__support.common
    libc.src.__support.macros.config
    .pwd_utils
)

add_entrypoint_object(
  setpwent
  SRCS
    setpwent.cpp
  HDRS
    setpwent.h
  DEPENDS
    libc.src.errno.errno
    libc.src.__support.common
    libc.src.__support.macros.config
    .pwd_utils
)

add_entrypoint_object(
  endpwent
  SRCS
    endpwent.cpp
  HDRS
    endpwent.h
  DEPENDS
    libc.src.errno.errno
    libc.src.__support.common
    libc.src.__support.macros.config
    .pwd_utils
)

add_object_library(
  pwd_utils
  HDRS
    field_tokenizer.h
    flat_file_db.h
    pwd_utils.h
  SRCS
    pwd_utils.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.stdio_macros
    libc.hdr.types.gid_t
    libc.hdr.types.size_t
    libc.hdr.types.struct_passwd
    libc.hdr.types.uid_t
    libc.src.string.string_utils
    libc.src.__support.CPP.functional
    libc.src.__support.CPP.optional
    libc.src.__support.CPP.span
    libc.src.__support.CPP.string_view
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
    libc.src.__support.ctype_utils
    libc.src.__support.error_or
    libc.src.__support.str_to_integer
    libc.src.__support.macros.attributes
    libc.src.__support.macros.config
)

add_header_library(
  field_tokenizer
  HDRS field_tokenizer.h
  DEPENDS
    libc.src.__support.CPP.optional
    libc.src.__support.CPP.span
    libc.src.__support.CPP.string_view
    libc.src.__support.macros.attributes
    libc.src.__support.macros.config
)
