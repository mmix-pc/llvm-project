add_custom_target(libc-wordexp-tests)
add_libc_test(
  parameter_test
  SUITE libc-wordexp-tests
  SRCS parameter_test.cpp
  DEPENDS libc.src.wordexp.parameter libc.hdr.errno_macros
)
add_libc_test(
  fields_test
  SUITE libc-wordexp-tests
  SRCS fields_test.cpp
  DEPENDS libc.src.wordexp.fields libc.hdr.errno_macros
)
add_libc_test(
  arithmetic_test
  SUITE libc-wordexp-tests
  SRCS arithmetic_test.cpp
  DEPENDS libc.src.wordexp.arithmetic libc.src.__support.CPP.limits
    libc.hdr.errno_macros
)
