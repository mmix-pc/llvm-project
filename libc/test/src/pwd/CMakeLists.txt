add_custom_target(libc_pwd_unittests)

if(NOT TARGET libc.src.pwd.pwd_utils)
  return()
endif()

add_libc_test(
  query_test
  SUITE libc_pwd_unittests
  SRCS
    query_test.cpp
    ${LIBC_SOURCE_DIR}/src/pwd/getpwnam_r.cpp
    ${LIBC_SOURCE_DIR}/src/pwd/getpwuid_r.cpp
    ${LIBC_SOURCE_DIR}/src/pwd/getpwnam.cpp
    ${LIBC_SOURCE_DIR}/src/pwd/getpwuid.cpp
  DEPENDS
    libc.src.pwd.lookup
    libc.src.__support.common
    libc.src.stdio.remove
)

add_libc_test(
  lookup_test
  SUITE libc_pwd_unittests
  SRCS lookup_test.cpp
  DEPENDS
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
    libc.src.__support.libc_errno
    libc.src.pwd.pwd_utils
    libc.src.pwd.lookup
    libc.src.stdio.remove
    libc.src.unistd.dup
    libc.src.unistd.close
)

add_libc_test(
  field_tokenizer_test
  SUITE
    libc_pwd_unittests
  SRCS
    field_tokenizer_test.cpp
  DEPENDS
    libc.src.__support.CPP.span
    libc.src.pwd.pwd_utils
)

add_libc_test(
  flat_file_db_test
  SUITE
    libc_pwd_unittests
  SRCS
    flat_file_db_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.src.__support.CPP.span
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
    libc.src.pwd.pwd_utils
    libc.src.stdio.remove
    libc.src.string.string_utils
)

add_libc_test(
  pwd_utils_test
  SUITE
    libc_pwd_unittests
  SRCS
    pwd_utils_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.struct_passwd
    libc.src.pwd.pwd_utils
)

add_libc_test(
  getpwent_test
  SUITE
    libc_pwd_unittests
  SRCS
    getpwent_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.types.struct_passwd
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
    libc.src.errno.errno
    libc.src.pwd.endpwent
    libc.src.pwd.getpwent
    libc.src.pwd.pwd_utils
    libc.src.pwd.setpwent
    libc.src.stdio.remove
    libc.src.string.string_utils
)
