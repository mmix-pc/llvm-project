add_custom_target(libc-fnmatch-tests)

add_libc_test(
  matcher_test
  SUITE libc-fnmatch-tests
  SRCS matcher_test.cpp
  DEPENDS libc.src.fnmatch.matcher libc.hdr.errno_macros
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.fnmatch.matcher_test.__build__)
  target_compile_definitions(libc.test.src.fnmatch.matcher_test.__build__
    PRIVATE PATH_MATCHER_ALLOCATION_TEST)
  target_link_options(libc.test.src.fnmatch.matcher_test.__build__
    PRIVATE -Wl,--wrap=malloc,--wrap=free)
endif()

add_libc_test(
  fnmatch_test
  SUITE
    libc-fnmatch-tests
  SRCS
    fnmatch_test.cpp
  DEPENDS
    libc.hdr.fnmatch_macros
    libc.src.fnmatch.fnmatch
)
