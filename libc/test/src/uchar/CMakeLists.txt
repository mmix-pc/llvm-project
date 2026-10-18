add_custom_target(libc_uchar_unittests)
add_libc_test(
  conversion_test
  SUITE libc_uchar_unittests
  SRCS conversion_test.cpp
  DEPENDS
    libc.src.uchar.c16rtomb
    libc.src.uchar.c32rtomb
    libc.src.uchar.mbrtoc16
    libc.src.uchar.mbrtoc32
    libc.src.wchar.mbsinit
    libc.src.__support.libc_errno
    libc.hdr.errno_macros
)
