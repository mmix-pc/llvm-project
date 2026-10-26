add_custom_target(libc-regex-component-tests)
add_libc_test(
  decoded_text_test
  FULL_BUILD_ONLY
  SUITE libc-regex-component-tests
  SRCS decoded_text_test.cpp
  DEPENDS libc.src.regex.decoded_text libc.src.__support.freelist_heap
    libc.hdr.errno_macros
)
if(TARGET libc.test.src.regex.decoded_text_test.__build__)
  target_link_options(libc.test.src.regex.decoded_text_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()

if(LLVM_LIBC_FULL_BUILD AND LLVM_LIBC_ENABLE_EXPERIMENTAL_ENTRYPOINTS)
  add_custom_target(libc_regex_unittests)

  add_libc_test(
    allocation_test
    SUITE libc_regex_unittests
    SRCS allocation_test.cpp
    DEPENDS libc.src.regex.bsd_engine libc.src.__support.freelist_heap
  )
  if(TARGET libc.test.src.regex.allocation_test.__build__)
    target_link_options(libc.test.src.regex.allocation_test.__build__ PRIVATE
      -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
  endif()

  add_libc_test(
    regex_boundary_test
    SUITE libc_regex_unittests
    SRCS regex_boundary_test.cpp
    DEPENDS libc.hdr.regex_macros libc.hdr.sys_mman_macros
      libc.hdr.unistd_macros libc.src.regex.regcomp libc.src.regex.regexec
      libc.src.regex.regfree libc.src.sys.mman.mmap libc.src.sys.mman.mprotect
      libc.src.sys.mman.munmap libc.src.unistd.sysconf
  )

  add_libc_test(
    regerror_test
    SUITE
      libc_regex_unittests
    SRCS
      regerror_test.cpp
    DEPENDS
      libc.hdr.regex_macros
      libc.src.regex.regerror
  )

  add_libc_test(
    regex_basic_test
    SUITE
      libc_regex_unittests
    SRCS
      regex_basic_test.cpp
    DEPENDS
      libc.hdr.locale_macros
      libc.src.locale.newlocale
      libc.src.locale.uselocale
      libc.src.locale.freelocale
      libc.hdr.regex_macros
      libc.hdr.types.regex_t
      libc.src.regex.regcomp
      libc.src.regex.regexec
      libc.src.regex.regfree
  )
endif()
