add_custom_target(libc-wordexp-tests)
add_libc_test(
  shell_syntax_test
  FULL_BUILD_ONLY
  SUITE libc-wordexp-tests
  SRCS shell_syntax_test.cpp
  DEPENDS libc.src.wordexp.shell_syntax libc.src.__support.freelist_heap
    libc.hdr.errno_macros
)
if(TARGET libc.test.src.wordexp.shell_syntax_test.__build__)
  target_link_options(libc.test.src.wordexp.shell_syntax_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()
if(TARGET libc.src.wordexp.paths)
  file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/paths-data)
  foreach(name a.txt b.txt .hidden star* "back\\one")
    file(TOUCH "${CMAKE_CURRENT_BINARY_DIR}/paths-data/${name}")
  endforeach()
  add_libc_test(
    paths_test
    FULL_BUILD_ONLY
    SUITE libc-wordexp-tests
    SRCS paths_test.cpp
    DEPENDS libc.src.wordexp.paths libc.src.__support.freelist_heap
      libc.hdr.errno_macros
  )
  if(TARGET libc.test.src.wordexp.paths_test.__build__)
    target_link_options(libc.test.src.wordexp.paths_test.__build__ PRIVATE
      -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
  endif()
endif()
if(TARGET libc.src.wordexp.command)
  add_libc_test(
    command_test
    FULL_BUILD_ONLY
    SUITE libc-wordexp-tests
    SRCS command_test.cpp
    DEPENDS libc.src.wordexp.command libc.hdr.errno_macros
      libc.hdr.fcntl_macros libc.hdr.sys_wait_macros
      libc.include.sys_syscall libc.src.__support.OSUtil.osutil
      libc.src.__support.freelist_heap
  )
  if(TARGET libc.test.src.wordexp.command_test.__build__)
    target_link_options(libc.test.src.wordexp.command_test.__build__ PRIVATE
      -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
  endif()
endif()
if(TARGET libc.src.wordexp.environment)
  add_libc_test(
    environment_test
    FULL_BUILD_ONLY
    SUITE libc-wordexp-tests
    SRCS environment_test.cpp ${LIBC_SOURCE_DIR}/src/wordexp/environment.cpp
    DEPENDS libc.src.wordexp.expand libc.src.stdlib.environ_internal
      libc.config.app_h libc.hdr.types.struct_passwd libc.hdr.errno_macros
      libc.src.__support.str_to_integer libc.src.__support.integer_to_string
  )
endif()
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
  ifs_test
  FULL_BUILD_ONLY
  SUITE libc-wordexp-tests
  SRCS ifs_test.cpp
  DEPENDS libc.src.wordexp.ifs libc.src.__support.freelist_heap
    libc.hdr.errno_macros
)
if(TARGET libc.test.src.wordexp.ifs_test.__build__)
  target_link_options(libc.test.src.wordexp.ifs_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()
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
