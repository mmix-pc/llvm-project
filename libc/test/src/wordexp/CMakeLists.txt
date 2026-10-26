add_custom_target(libc-wordexp-tests)
add_libc_test(
  fragments_test
  FULL_BUILD_ONLY
  SUITE libc-wordexp-tests
  SRCS fragments_test.cpp
  DEPENDS libc.src.wordexp.expand libc.src.__support.freelist_heap
    libc.hdr.errno_macros
)
if(TARGET libc.test.src.wordexp.fragments_test.__build__)
  target_link_options(libc.test.src.wordexp.fragments_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()
add_libc_test(
  expand_test
  SUITE libc-wordexp-tests
  SRCS expand_test.cpp
  DEPENDS libc.src.wordexp.expand libc.hdr.errno_macros
)
add_libc_test(
  pattern_test
  SUITE libc-wordexp-tests
  SRCS pattern_test.cpp
  DEPENDS libc.src.wordexp.pattern libc.hdr.errno_macros
)
add_libc_test(
  dollar_quote_test
  SUITE libc-wordexp-tests
  SRCS dollar_quote_test.cpp
  DEPENDS libc.src.wordexp.dollar_quote libc.hdr.errno_macros
)
add_libc_test(
  results_test
  FULL_BUILD_ONLY
  SUITE libc-wordexp-tests
  SRCS results_test.cpp
  DEPENDS libc.src.wordexp.results libc.src.__support.freelist_heap
    libc.hdr.errno_macros
)
if(TARGET libc.test.src.wordexp.results_test.__build__)
  target_link_options(libc.test.src.wordexp.results_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()
add_libc_test(
  syntax_test
  SUITE libc-wordexp-tests
  SRCS syntax_test.cpp
  DEPENDS libc.src.wordexp.syntax libc.hdr.errno_macros
)
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
