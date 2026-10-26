add_custom_target(libc-libintl-tests)
add_libc_test(
  message_results_test
  SUITE libc-libintl-tests
  SRCS message_results_test.cpp
  DEPENDS libc.src.libintl.message_results
)
if(LIBC_TARGET_OS_IS_LINUX AND
   TARGET libc.test.src.libintl.message_results_test.__build__)
  target_compile_definitions(libc.test.src.libintl.message_results_test.__build__
    PRIVATE MESSAGE_RESULTS_ALLOCATION_TEST)
  target_link_options(libc.test.src.libintl.message_results_test.__build__
    PRIVATE -Wl,--wrap=malloc,--wrap=free)
endif()
if(TARGET libc.src.libintl.message_file)
  add_libc_test(
    message_file_test
    SUITE libc-libintl-tests
    SRCS message_file_test.cpp
    COMPILE_OPTIONS -DMESSAGE_FILE_LINUX_TEST
    DEPENDS libc.src.libintl.message_file libc.src.stdlib.mkdtemp
      libc.src.__support.OSUtil.osutil libc.include.sys_syscall
  )
  if(TARGET libc.test.src.libintl.message_file_test.__build__)
    target_compile_definitions(libc.test.src.libintl.message_file_test.__build__
      PRIVATE MESSAGE_FILE_ALLOCATION_TEST)
    target_link_options(libc.test.src.libintl.message_file_test.__build__
      PRIVATE -Wl,--wrap=malloc,--wrap=free)
  endif()
endif()
if(TARGET libc.src.libintl.message_path)
  add_libc_test(
    message_path_test
    SUITE libc-libintl-tests
    SRCS message_path_test.cpp
    DEPENDS libc.src.libintl.message_path
  )
endif()
add_libc_test(
  message_translation_test
  SUITE libc-libintl-tests
  SRCS message_translation_test.cpp
  DEPENDS libc.src.libintl.message_translation libc.hdr.func.free
    libc.hdr.errno_macros
)
if(LIBC_TARGET_OS_IS_LINUX AND
   TARGET libc.test.src.libintl.message_translation_test.__build__)
  target_compile_definitions(libc.test.src.libintl.message_translation_test.__build__
    PRIVATE MESSAGE_TRANSLATION_ALLOCATION_TEST)
  target_link_options(libc.test.src.libintl.message_translation_test.__build__
    PRIVATE -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()
add_libc_test(
  plural_forms_test
  SUITE libc-libintl-tests
  SRCS plural_forms_test.cpp
  DEPENDS libc.src.libintl.plural_forms libc.src.__support.CPP.limits
    libc.hdr.errno_macros
)
set(domain_binding_deps)
set(domain_binding_options)
if(TARGET libc.src.pthread.pthread_create AND TARGET libc.src.pthread.pthread_join)
  list(APPEND domain_binding_deps libc.src.pthread.pthread_create
    libc.src.pthread.pthread_join)
  list(APPEND domain_binding_options -DDOMAIN_BINDINGS_THREADS)
endif()
add_libc_test(
  domain_bindings_test
  SUITE libc-libintl-tests
  SRCS domain_bindings_test.cpp
  COMPILE_OPTIONS ${domain_binding_options}
  DEPENDS libc.src.libintl.textdomain libc.src.libintl.bindtextdomain
    libc.src.libintl.bind_textdomain_codeset libc.src.__support.libc_errno
    libc.src.__support.CPP.string_view libc.hdr.errno_macros
    ${domain_binding_deps}
)
if(LIBC_TARGET_OS_IS_LINUX AND
   TARGET libc.test.src.libintl.domain_bindings_test.__build__)
  target_compile_definitions(libc.test.src.libintl.domain_bindings_test.__build__
    PRIVATE DOMAIN_BINDINGS_ALLOCATION_TEST)
  target_link_options(libc.test.src.libintl.domain_bindings_test.__build__
    PRIVATE -Wl,--wrap=malloc,--wrap=free)
endif()
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
