add_custom_target(libc-nl-types-tests)

add_libc_test(
  catalog_path_test
  SUITE libc-nl-types-tests
  SRCS catalog_path_test.cpp
  DEPENDS libc.src.nl_types.catalog_path
)
if(LIBC_TARGET_OS_IS_LINUX)
  add_libc_test(
    catalog_linux_test
    SUITE libc-nl-types-tests
    SRCS catalog_linux_test.cpp
    DEPENDS libc.src.nl_types.catopen libc.src.nl_types.catgets
      libc.src.nl_types.catclose libc.src.stdlib.mkdtemp
      libc.src.stdlib.environ_internal libc.src.locale.newlocale
      libc.src.locale.freelocale libc.src.locale.uselocale
      libc.src.__support.OSUtil.linux.auxv libc.config.app_h
  )
  if(TARGET libc.test.src.nl_types.catalog_linux_test.__build__)
    target_compile_definitions(libc.test.src.nl_types.catalog_linux_test.__build__ PRIVATE
      CATALOG_OPEN_ALLOCATION_TEST)
    target_link_options(libc.test.src.nl_types.catalog_linux_test.__build__ PRIVATE
      -Wl,--wrap=malloc,--wrap=free)
  endif()
endif()

add_libc_test(
  catalog_data_test
  SUITE libc-nl-types-tests
  SRCS catalog_data_test.cpp
  DEPENDS libc.src.nl_types.catalog_data libc.hdr.errno_macros
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.nl_types.catalog_data_test.__build__)
  target_compile_definitions(libc.test.src.nl_types.catalog_data_test.__build__ PRIVATE
    CATALOG_ALLOCATION_TEST)
  target_link_options(libc.test.src.nl_types.catalog_data_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=free)
endif()

add_libc_test(
  nl_types_test
  SUITE
    libc-nl-types-tests
  SRCS
    nl_types_test.cpp
  DEPENDS
    libc.include.llvm-libc-macros.nl_types_macros
    libc.include.llvm-libc-types.nl_catd
    libc.src.nl_types.catopen
    libc.src.nl_types.catclose
    libc.src.nl_types.catgets
)
