# Storage mode does not imply public thread creation or concurrent libc support.
option(LIBC_MMIX_ENABLE_STATIC_TLS "Build the main-thread static TLS profile" OFF)
function(mmix_check_runtime_mode)
  if(LIBC_MMIX_ENABLE_STATIC_TLS)
    if(NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM" OR
       NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_THREAD_LOCAL")
      message(FATAL_ERROR "MMIX static TLS requires PLATFORM storage and THREAD_LOCAL errno")
    endif()
    if(NOT "-ftls-model=local-exec" IN_LIST LIBC_COMPILE_OPTIONS_DEFAULT OR
       NOT "-fno-lto" IN_LIST LIBC_COMPILE_OPTIONS_DEFAULT)
      message(FATAL_ERROR "MMIX static TLS requires native local-exec runtime providers")
    endif()
    foreach(component FORK VFORK SPAWN ACCOUNT_MANAGEMENT SIGNAL_RUNTIME
                      COMMAND_ENVIRONMENT_RUNTIME)
      if(LIBC_MMIX_BUILD_${component})
        message(FATAL_ERROR "MMIX static TLS does not yet support ${component}")
      endif()
    endforeach()
    if(LIBC_MMIX_INSTALL_RUNTIME)
      message(FATAL_ERROR "MMIX static TLS is not an installed runtime profile")
    endif()
  elseif(NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE" OR
         NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_SHARED")
    message(FATAL_ERROR "MMIX Linux requires SINGLE/shared state or explicit static TLS")
  endif()
endfunction()
