# Configuration acceptance is not runtime or installation qualification.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "mmix" OR NOT CMAKE_CROSSCOMPILING OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR
   NOT CMAKE_TRY_COMPILE_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "MMIX shared C++ requires cross Linux Release and static compiler probes")
endif()
set(selected_runtimes ${LLVM_ENABLE_RUNTIMES})
list(SORT selected_runtimes)
if(NOT selected_runtimes STREQUAL "libcxx;libcxxabi;libunwind")
  message(FATAL_ERROR "MMIX shared C++ requires all three C++ runtimes")
endif()
foreach(option CMAKE_POSITION_INDEPENDENT_CODE CMAKE_SKIP_RPATH
               LIBCXX_ENABLE_THREADS LIBCXX_HAS_PTHREAD_API
               LIBCXX_ENABLE_EXCEPTIONS LIBCXX_ENABLE_RTTI LIBCXX_USE_COMPILER_RT
               LIBCXXABI_ENABLE_THREADS LIBCXXABI_HAS_PTHREAD_API
               LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL LIBCXXABI_ENABLE_EXCEPTIONS
               LIBCXXABI_ENABLE_RTTI LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS
               LIBCXXABI_USE_COMPILER_RT LIBCXXABI_USE_LLVM_UNWINDER
               LIBUNWIND_ENABLE_THREADS LIBUNWIND_USE_COMPILER_RT
               LIBUNWIND_IS_NATIVE_ONLY LIBUNWIND_REMEMBER_HEAP_ALLOC)
  if(NOT ${option})
    message(FATAL_ERROR "MMIX shared C++ requires ${option}=ON")
  endif()
endforeach()
foreach(option LIBCXX_HAS_EXTERNAL_THREAD_API LIBCXX_HAS_C11_THREAD_API
               LIBCXX_HAS_WIN32_THREAD_API LIBCXXABI_HAS_EXTERNAL_THREAD_API
               LIBCXXABI_HAS_WIN32_THREAD_API LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS
               LIBCXX_ENABLE_STATIC_ABI_LIBRARY
               LIBCXX_STATICALLY_LINK_ABI_IN_SHARED_LIBRARY
               LIBCXXABI_STATICALLY_LINK_UNWINDER_IN_SHARED_LIBRARY
               LIBCXXABI_BAREMETAL LIBUNWIND_IS_BAREMETAL
               LIBUNWIND_USE_FRAME_HEADER_CACHE LIBUNWIND_ENABLE_FRAME_APIS
               LIBUNWIND_WEAK_PTHREAD_LIB LIBUNWIND_HIDE_SYMBOLS
               LIBUNWIND_ENABLE_ASSERTIONS LIBCXX_MMIX_LINUX_INSTALL_RUNTIME
               LIBCXX_MMIX_LINUX_THREADED_PREPARATION LIBCXX_MMIX_LINUX_STATE_DIR
               LIBCXXABI_MMIX_LINUX_TLS_DESTRUCTORS
               LIBCXXABI_MMIX_LINUX_CANCELLATION_SAFE_GUARDS
               LIBCXX_INSTALL_MODULES LIBCXX_INCLUDE_BENCHMARKS
               CMAKE_INTERPROCEDURAL_OPTIMIZATION
               CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE LLVM_USE_SANITIZER)
  if(${option})
    message(FATAL_ERROR "MMIX shared C++ preparation rejects ${option}")
  endif()
endforeach()
if(NOT LIBCXX_CXX_ABI STREQUAL "libcxxabi")
  message(FATAL_ERROR "MMIX shared C++ requires libc++abi")
endif()
foreach(project LIBCXX LIBCXXABI LIBUNWIND)
  if(NOT ${project}_ENABLE_SHARED OR ${project}_ENABLE_STATIC)
    message(FATAL_ERROR "MMIX shared C++ requires shared-only runtime targets")
  endif()
  foreach(option INSTALL_HEADERS INSTALL_LIBRARY INCLUDE_TESTS
                 HAS_C_LIB HAS_PTHREAD_LIB HAS_DL_LIB HAS_RT_LIB
                 HAS_ATOMIC_LIB HAS_GCC_LIB HAS_GCC_S_LIB)
    if(${project}_${option})
      message(FATAL_ERROR "MMIX shared C++ preparation rejects ${project}_${option}")
    endif()
  endforeach()
endforeach()
foreach(kind LIBRARY INCLUDE PACKAGE)
  if(NOT CMAKE_FIND_ROOT_PATH_MODE_${kind} STREQUAL "ONLY")
    message(FATAL_ERROR "MMIX shared C++ requires sysroot-only ${kind} searches")
  endif()
endforeach()
foreach(flag -D_LIBUNWIND_MMIX_LINUX_THREADS -D_LIBUNWIND_MMIX_STARTUP_IMAGES)
  if(NOT flag IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
    message(FATAL_ERROR "MMIX shared C++ requires ${flag}")
  endif()
endforeach()
foreach(language C CXX ASM)
  if(NOT CMAKE_${language}_COMPILER_TARGET STREQUAL "mmix-unknown-linux")
    message(FATAL_ERROR "MMIX shared C++ requires matching Linux compiler targets")
  endif()
  separate_arguments(flags NATIVE_COMMAND "${CMAKE_${language}_FLAGS}")
  if(NOT "-fPIC" IN_LIST flags)
    message(FATAL_ERROR "MMIX shared C++ requires explicit PIC ${language} flags")
  endif()
  if(NOT language STREQUAL "ASM")
    foreach(flag -ftls-model=initial-exec -fno-lto)
      if(NOT flag IN_LIST flags)
        message(FATAL_ERROR "MMIX shared C++ requires ${flag}")
      endif()
    endforeach()
  endif()
  foreach(kind FLAGS FLAGS_RELEASE)
    set(flags "${CMAKE_${language}_${kind}}")
    string(REPLACE "-ftls-model=initial-exec" "" flags "${flags}")
    if(flags MATCHES "-ftls-model=|-femulated-tls|-flto|-fno-[Pp][Ii][CcEe]|-fsanitize|-fno-exceptions|-fno-rtti|--?target[= ]|--?sysroot[= ]|--?stdlib=|--?rtlib=")
      message(FATAL_ERROR "MMIX shared C++ rejects conflicting ${language} ${kind}")
    endif()
  endforeach()
endforeach()
separate_arguments(link_flags NATIVE_COMMAND "${CMAKE_SHARED_LINKER_FLAGS}")
if(NOT "-nostdlib" IN_LIST link_flags)
  message(FATAL_ERROR "MMIX shared C++ requires explicit link providers (-nostdlib)")
endif()
