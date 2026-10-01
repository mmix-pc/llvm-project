# Storage mode does not imply public thread creation or concurrent libc support.
option(LIBC_MMIX_ENABLE_STATIC_TLS "Build the main-thread static TLS profile" OFF)
option(LIBC_MMIX_BUILD_THREAD_LIFECYCLE
  "Build the non-installed internal Linux thread lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_CREATION
  "Add public pthread creation and identity to the lifecycle archive" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE
  "Add public pthread join, detach and C thread exit" OFF)
option(LIBC_MMIX_BUILD_PTHREAD_MUTEX
  "Add public private normal mutexes to the lifecycle archive" OFF)
function(mmix_check_runtime_mode)
  if(LIBC_MMIX_BUILD_PTHREAD_MUTEX AND NOT LIBC_MMIX_BUILD_PTHREAD_CREATION)
    message(FATAL_ERROR "MMIX public mutexes require the pthread creation profile")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE AND NOT LIBC_MMIX_BUILD_PTHREAD_CREATION)
    message(FATAL_ERROR "MMIX public pthread lifecycle requires pthread creation")
  endif()
  if(LIBC_MMIX_BUILD_PTHREAD_CREATION AND NOT LIBC_MMIX_BUILD_THREAD_LIFECYCLE)
    message(FATAL_ERROR "MMIX pthread creation requires the internal lifecycle")
  endif()
  if(LIBC_MMIX_BUILD_THREAD_LIFECYCLE)
    if(NOT LIBC_MMIX_ENABLE_STATIC_TLS OR NOT LIBC_MMIX_BUILD_RUNTIME_STATE)
      message(FATAL_ERROR "MMIX internal lifecycle requires static TLS and runtime state")
    endif()
    # Do not make unreviewed concurrent libc consumers reachable through this
    # preparation archive. Full public libc/pthread composition is separate.
    get_cmake_property(variables VARIABLES)
    foreach(variable IN LISTS variables)
      if(variable MATCHES "^LIBC_MMIX_BUILD_" AND ${variable} AND
         NOT variable MATCHES "^LIBC_MMIX_BUILD_(THREAD_LIFECYCLE|RUNTIME_STATE|PTHREAD_CREATION|PTHREAD_LIFECYCLE|PTHREAD_MUTEX)$")
        message(FATAL_ERROR "MMIX internal lifecycle does not support ${variable}")
      endif()
    endforeach()
  endif()
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
