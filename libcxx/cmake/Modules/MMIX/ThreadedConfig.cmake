# This validates preparation settings, not provider completeness or execution.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "mmix" OR
   NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR
   NOT CMAKE_TRY_COMPILE_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "MMIX pthread C++ requires cross Release and static compiler probes")
endif()
foreach(language C CXX ASM)
  if(NOT CMAKE_${language}_COMPILER_TARGET STREQUAL "mmix-unknown-linux")
    message(FATAL_ERROR "MMIX pthread C++ requires matching Linux compiler targets")
  endif()
endforeach()
set(selected_runtimes ${LLVM_ENABLE_RUNTIMES})
list(SORT selected_runtimes)
if(NOT selected_runtimes STREQUAL "libcxx;libcxxabi;libunwind")
  message(FATAL_ERROR "MMIX pthread C++ requires all three C++ runtimes")
endif()
foreach(option LIBCXX_MMIX_LINUX_THREADED_PREPARATION
               LIBCXX_ENABLE_THREADS LIBCXX_HAS_PTHREAD_API
               LIBCXX_ENABLE_STATIC LIBCXX_ENABLE_EXCEPTIONS LIBCXX_ENABLE_RTTI
               LIBCXX_USE_COMPILER_RT LIBCXXABI_ENABLE_THREADS
               LIBCXXABI_HAS_PTHREAD_API LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL
               LIBCXXABI_ENABLE_STATIC LIBCXXABI_ENABLE_EXCEPTIONS
               LIBCXXABI_ENABLE_RTTI LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS
               LIBCXXABI_USE_COMPILER_RT LIBCXXABI_USE_LLVM_UNWINDER
               LIBUNWIND_ENABLE_THREADS LIBUNWIND_ENABLE_STATIC
               LIBUNWIND_USE_COMPILER_RT LIBUNWIND_IS_NATIVE_ONLY
               LIBUNWIND_REMEMBER_HEAP_ALLOC)
  if(NOT ${option})
    message(FATAL_ERROR "MMIX pthread C++ requires ${option}=ON")
  endif()
endforeach()
foreach(option LIBCXX_MMIX_LINUX_INSTALL_RUNTIME LIBCXX_HAS_EXTERNAL_THREAD_API
               LIBCXX_HAS_C11_THREAD_API LIBCXX_HAS_WIN32_THREAD_API
               LIBCXXABI_HAS_EXTERNAL_THREAD_API LIBCXXABI_HAS_WIN32_THREAD_API
               LIBCXX_ENABLE_SHARED LIBCXXABI_ENABLE_SHARED LIBUNWIND_ENABLE_SHARED
               LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS LIBCXX_ENABLE_STATIC_ABI_LIBRARY
               LIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY
               LIBUNWIND_USE_FRAME_HEADER_CACHE LIBUNWIND_ENABLE_FRAME_APIS
               LIBUNWIND_WEAK_PTHREAD_LIB LIBUNWIND_ENABLE_ASSERTIONS
               LIBCXXABI_BAREMETAL LIBUNWIND_IS_BAREMETAL
               LIBCXX_INSTALL_MODULES LIBCXX_INCLUDE_BENCHMARKS
               CMAKE_POSITION_INDEPENDENT_CODE BUILD_SHARED_LIBS
               CMAKE_INTERPROCEDURAL_OPTIMIZATION
               CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE LLVM_USE_SANITIZER)
  if(${option})
    message(FATAL_ERROR "MMIX pthread C++ rejects ${option}")
  endif()
endforeach()
foreach(project LIBCXX LIBCXXABI LIBUNWIND)
  foreach(option INSTALL_HEADERS INSTALL_LIBRARY INCLUDE_TESTS
                 HAS_PTHREAD_LIB HAS_DL_LIB HAS_GCC_LIB HAS_GCC_S_LIB
                 HAS_RT_LIB HAS_ATOMIC_LIB)
    if(${project}_${option})
      message(FATAL_ERROR "MMIX pthread C++ rejects ${project}_${option}")
    endif()
  endforeach()
endforeach()
foreach(kind LIBRARY INCLUDE PACKAGE)
  if(NOT CMAKE_FIND_ROOT_PATH_MODE_${kind} STREQUAL "ONLY")
    message(FATAL_ERROR "MMIX pthread C++ requires sysroot-only ${kind} searches")
  endif()
endforeach()
foreach(language C CXX)
  separate_arguments(flags NATIVE_COMMAND "${CMAKE_${language}_FLAGS}")
  if(NOT "-ftls-model=local-exec" IN_LIST flags OR NOT "-fno-lto" IN_LIST flags)
    message(FATAL_ERROR "MMIX pthread C++ requires native local-exec ${language} flags")
  endif()
endforeach()
foreach(language C CXX ASM)
  foreach(kind FLAGS FLAGS_RELEASE)
    set(flags "${CMAKE_${language}_${kind}}")
    string(REGEX REPLACE "-ftls-model=local-exec" "" flags "${flags}")
    if(flags MATCHES "-ftls-model=|-femulated-tls|-flto|-f[Pp][Ii][CcEe]|-fsanitize|-fno-exceptions|-fno-rtti|--?target[= ]|--?sysroot[= ]|--?stdlib=|--?rtlib=")
      message(FATAL_ERROR "MMIX pthread C++ rejects conflicting ${language} ${kind}")
    endif()
  endforeach()
endforeach()
if(LIBCXX_MMIX_LINUX_STATE_DIR)
  message(FATAL_ERROR "MMIX pthread C++ rejects the external-thread state adapter")
endif()
