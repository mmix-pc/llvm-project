add_custom_target(libc-monetary-tests)
add_libc_test(
  strfmon_test
  SUITE libc-monetary-tests
  SRCS strfmon_test.cpp
  DEPENDS libc.src.monetary.strfmon libc.src.monetary.strfmon_l
    libc.src.locale.locale libc.src.locale.uselocale
    libc.src.__support.libc_errno libc.src.fenv.fesetround
    libc.src.fenv.fegetround
)
if(LIBC_TARGET_OS_IS_LINUX AND TARGET libc.test.src.monetary.strfmon_test.__build__)
  target_compile_definitions(libc.test.src.monetary.strfmon_test.__build__
    PRIVATE MONETARY_ALLOCATION_TEST)
  target_link_options(libc.test.src.monetary.strfmon_test.__build__
    PRIVATE -Wl,--wrap=malloc,--wrap=free)
endif()
