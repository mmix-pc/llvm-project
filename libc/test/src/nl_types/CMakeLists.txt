add_custom_target(libc-nl-types-tests)

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
