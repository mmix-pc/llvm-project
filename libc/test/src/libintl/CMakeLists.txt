add_custom_target(libc-libintl-tests)
add_libc_test(
  mo_catalog_test
  SUITE libc-libintl-tests
  SRCS mo_catalog_test.cpp
  DEPENDS libc.src.libintl.mo_catalog libc.hdr.errno_macros
)
if(LIBC_TARGET_OS_IS_LINUX AND
   TARGET libc.test.src.libintl.mo_catalog_test.__build__)
  target_compile_definitions(
    libc.test.src.libintl.mo_catalog_test.__build__
    PRIVATE MO_CATALOG_ALLOCATION_TEST)
  target_link_options(
    libc.test.src.libintl.mo_catalog_test.__build__
    PRIVATE -Wl,--wrap=malloc,--wrap=free)
endif()
