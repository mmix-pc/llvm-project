if(NOT LIBC_TARGET_OS_IS_LINUX)
  return()
endif()
add_custom_target(libc-iconv-tests)
add_libc_test(
  iconv_test
  SUITE libc-iconv-tests
  SRCS iconv_test.cpp
  DEPENDS libc.src.iconv.iconv_open libc.src.iconv.iconv libc.src.iconv.iconv_close
    libc.src.locale.locale libc.src.__support.libc_errno
  LINK_LIBRARIES -Wl,--wrap=malloc -Wl,--wrap=free
)
