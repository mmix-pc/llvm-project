# Validate build-tree inputs before the runtime projects create their targets.
include_guard(GLOBAL)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "mmix" OR
   NOT CMAKE_CROSSCOMPILING OR NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR
   NOT CMAKE_TRY_COMPILE_TARGET_TYPE STREQUAL "STATIC_LIBRARY")
  message(FATAL_ERROR "MMIX Linux C++ composition requires cross Release and static compiler probes")
endif()
foreach(language C CXX ASM)
  if(NOT CMAKE_${language}_COMPILER_TARGET STREQUAL "mmix-unknown-linux")
    message(FATAL_ERROR "MMIX Linux C++ composition requires matching Linux compiler targets")
  endif()
endforeach()
foreach(kind LIBRARY INCLUDE PACKAGE)
  if(NOT CMAKE_FIND_ROOT_PATH_MODE_${kind} STREQUAL "ONLY")
    message(FATAL_ERROR "MMIX Linux C++ composition requires sysroot-only ${kind} searches")
  endif()
endforeach()
set(selected_runtimes ${LLVM_ENABLE_RUNTIMES})
list(SORT selected_runtimes)
if(NOT selected_runtimes STREQUAL "libcxx;libcxxabi;libunwind")
  message(FATAL_ERROR "MMIX Linux C++ composition requires libcxx, libcxxabi and libunwind")
endif()
foreach(option LIBCXX_ENABLE_STATIC LIBCXX_ENABLE_EXCEPTIONS LIBCXX_ENABLE_RTTI
               LIBCXX_USE_COMPILER_RT LIBCXX_ENABLE_THREADS LIBCXX_HAS_EXTERNAL_THREAD_API
               LIBCXX_ENABLE_MONOTONIC_CLOCK LIBCXX_ENABLE_WIDE_CHARACTERS LIBCXXABI_ENABLE_STATIC
               LIBCXXABI_ENABLE_EXCEPTIONS LIBCXXABI_ENABLE_RTTI
               LIBCXXABI_ENABLE_NEW_DELETE_DEFINITIONS LIBCXXABI_USE_COMPILER_RT
               LIBCXXABI_USE_LLVM_UNWINDER LIBUNWIND_ENABLE_STATIC
               LIBUNWIND_USE_COMPILER_RT LIBUNWIND_IS_NATIVE_ONLY
               LIBUNWIND_REMEMBER_HEAP_ALLOC)
  if(NOT ${option})
    message(FATAL_ERROR "MMIX Linux C++ composition requires ${option}=ON")
  endif()
endforeach()
foreach(option LIBCXX_ENABLE_SHARED LIBCXXABI_ENABLE_SHARED LIBUNWIND_ENABLE_SHARED
               LIBCXXABI_ENABLE_THREADS LIBUNWIND_ENABLE_THREADS
               LIBCXX_HAS_PTHREAD_API LIBCXX_HAS_C11_THREAD_API LIBCXX_HAS_WIN32_THREAD_API
               LIBCXXABI_HAS_EXTERNAL_THREAD_API LIBCXXABI_HAS_PTHREAD_API
               LIBCXXABI_HAS_WIN32_THREAD_API LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL
               LIBCXX_ENABLE_NEW_DELETE_DEFINITIONS LIBCXX_ENABLE_STATIC_ABI_LIBRARY
               LIBCXX_HAS_RT_LIB LIBCXX_HAS_ATOMIC_LIB
               LIBCXX_STATICALLY_LINK_ABI_IN_STATIC_LIBRARY LIBCXX_ABI_UNSTABLE
               LIBCXX_ENABLE_FILESYSTEM LIBCXX_ENABLE_LOCALIZATION LIBCXX_ENABLE_RANDOM_DEVICE
               LIBCXX_ENABLE_TIME_ZONE_DATABASE LIBCXX_ENABLE_UNICODE
               LIBCXX_INSTALL_MODULES LIBCXX_INCLUDE_BENCHMARKS
               LIBUNWIND_USE_FRAME_HEADER_CACHE LIBUNWIND_ENABLE_FRAME_APIS
               LIBCXXABI_BAREMETAL LIBUNWIND_IS_BAREMETAL
               CMAKE_POSITION_INDEPENDENT_CODE BUILD_SHARED_LIBS
               CMAKE_INTERPROCEDURAL_OPTIMIZATION CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE
               LLVM_USE_SANITIZER)
  if(${option})
    message(FATAL_ERROR "MMIX Linux C++ composition requires ${option}=OFF")
  endif()
endforeach()
foreach(project LIBCXX LIBCXXABI LIBUNWIND)
  foreach(option INSTALL_HEADERS INSTALL_LIBRARY INCLUDE_TESTS HAS_PTHREAD_LIB HAS_DL_LIB HAS_GCC_LIB HAS_GCC_S_LIB)
    if(${project}_${option})
      message(FATAL_ERROR "MMIX Linux C++ composition rejects ${project}_${option}")
    endif()
  endforeach()
endforeach()
foreach(language C CXX ASM)
  file(REAL_PATH "${CMAKE_${language}_COMPILER}" selected_compiler)
  file(REAL_PATH "${CMAKE_C_COMPILER}" c_compiler)
  if(NOT selected_compiler STREQUAL c_compiler)
    message(FATAL_ERROR "MMIX Linux C++ composition requires one compiler for C, C++ and assembly")
  endif()
  foreach(flags CMAKE_${language}_FLAGS CMAKE_${language}_FLAGS_RELEASE)
    if("${${flags}}" MATCHES "-flto|-f[Pp][Ii][CcEe]|-fsanitize|-finstrument-functions|-fprofile|-fno-exceptions|-fno-rtti|--?target=|--?sysroot=|--?stdlib=|--?rtlib=")
      message(FATAL_ERROR "MMIX Linux C++ composition rejects conflicting producer flags in ${flags}")
    endif()
  endforeach()
endforeach()
if(NOT LIBCXX_CXX_ABI STREQUAL "libcxxabi" OR
   NOT LIBCXX_ABI_VERSION STREQUAL "1" OR NOT LIBCXX_ABI_NAMESPACE STREQUAL "__1" OR
   NOT LIBCXX_PSTL_BACKEND STREQUAL "serial")
  message(FATAL_ERROR "MMIX Linux C++ composition requires the selected libc++ ABI and serial backend")
endif()

# These manifests are trusted CMake build products, not installed packages.
function(mmix_runtime_file root path)
  if(NOT IS_ABSOLUTE "${root}" OR NOT IS_ABSOLUTE "${path}" OR
     NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(FATAL_ERROR "MMIX Linux C++ composition requires an existing absolute input: ${path}")
  endif()
  file(REAL_PATH "${root}" root)
  file(REAL_PATH "${path}" path)
  string(FIND "${path}" "${root}/" inside)
  if(NOT inside EQUAL 0)
    message(FATAL_ERROR "MMIX Linux C++ composition input escapes its provider root: ${path}")
  endif()
endfunction()
set(libc_build "${LIBCXX_MMIX_LINUX_LIBC_BUILD}")
set(rt_build "${LIBCXX_MMIX_LINUX_COMPILER_RT_BUILD}")
foreach(input mmix-signal-runtime/Inputs.cmake mmix-c-runtime/Inputs.cmake
              mmix-state/LibcStateConfig.cmake)
  mmix_runtime_file("${libc_build}" "${libc_build}/${input}")
endforeach()
include("${libc_build}/mmix-signal-runtime/Inputs.cmake")
if(NOT MMIX_SIGNAL_RUNTIME_PRODUCER_POLICY STREQUAL "mmix-linux-static-single-thread" OR
   NOT MMIX_SIGNAL_RUNTIME_DOMAIN_POLICY STREQUAL "kernel-owned-sync-query-jump")
  message(FATAL_ERROR "MMIX Linux C++ composition requires the selected signal producer profile")
endif()
foreach(pair "${MMIX_C_RUNTIME_SYSROOT}|${CMAKE_SYSROOT}"
             "${MMIX_C_RUNTIME_COMPILER}|${CMAKE_CXX_COMPILER}"
             "${MMIX_C_RUNTIME_SOURCE}|${CMAKE_CURRENT_LIST_DIR}/../../../../libc")
  string(REPLACE "|" ";" paths "${pair}")
  list(GET paths 0 supplied)
  list(GET paths 1 selected)
  file(REAL_PATH "${supplied}" supplied)
  file(REAL_PATH "${selected}" selected)
  if(NOT supplied STREQUAL selected)
    message(FATAL_ERROR "MMIX Linux C++ composition requires matching source, compiler and sysroot")
  endif()
endforeach()
foreach(kind LIBC STATE CRT1)
  mmix_runtime_file("${libc_build}" "${MMIX_C_RUNTIME_${kind}}")
endforeach()
foreach(kind BUILTINS CRTBEGIN CRTEND)
  mmix_runtime_file("${rt_build}" "${MMIX_C_RUNTIME_${kind}}")
endforeach()
# Reject host objects before a static archive can conceal the wrong target.
get_filename_component(compiler_bin "${CMAKE_C_COMPILER}" DIRECTORY)
foreach(kind LIBC STATE CRT1 BUILTINS CRTBEGIN CRTEND)
  execute_process(COMMAND "${compiler_bin}/llvm-readobj" --file-headers
    "${MMIX_C_RUNTIME_${kind}}" RESULT_VARIABLE status OUTPUT_VARIABLE headers
    ERROR_VARIABLE diagnostics)
  string(REGEX MATCHALL "Format: [^\n]+" formats "${headers}")
  if(NOT status EQUAL 0 OR NOT formats)
    message(FATAL_ERROR "Cannot inspect MMIX Linux C++ ${kind} input: ${diagnostics}")
  endif()
  foreach(format IN LISTS formats)
    if(NOT format STREQUAL "Format: elf64-mmix")
      message(FATAL_ERROR "MMIX Linux C++ composition requires MMIX ELF inputs: ${kind}")
    endif()
  endforeach()
endforeach()
mmix_runtime_file("${rt_build}" "${rt_build}/CMakeCache.txt")
load_cache("${rt_build}" READ_WITH_PREFIX MMIX_RT_
  CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET CMAKE_C_COMPILER)
file(REAL_PATH "${MMIX_RT_CMAKE_C_COMPILER}" rt_compiler)
file(REAL_PATH "${CMAKE_C_COMPILER}" compiler)
if(NOT MMIX_RT_CMAKE_C_COMPILER_TARGET STREQUAL "mmix-unknown-linux" OR
   NOT MMIX_RT_CMAKE_CXX_COMPILER_TARGET STREQUAL "mmix-unknown-linux" OR
   NOT rt_compiler STREQUAL compiler)
  message(FATAL_ERROR "MMIX Linux C++ composition requires matching Linux compiler-rt")
endif()
foreach(header errno.h stdlib.h stdio.h string.h wchar.h wctype.h time.h signal.h sys/syscall.h)
  mmix_runtime_file("${libc_build}" "${MMIX_C_RUNTIME_HEADERS}/${header}")
endforeach()
foreach(name swprintf wcslen wmemcmp wmemcpy wmemmove wmemset wmemchr
             wcstol wcstoll wcstoul wcstoull wcstof wcstod wcstold)
  if(NOT "libc.src.wchar.${name}" IN_LIST MMIX_C_RUNTIME_ENTRYPOINTS)
    message(FATAL_ERROR "MMIX Linux C++ composition requires wide libc provider ${name}")
  endif()
endforeach()
foreach(header asm/unistd.h asm/rstack.h asm/sigcontext.h)
  mmix_runtime_file("${CMAKE_SYSROOT}"
    "${CMAKE_SYSROOT}${MMIX_C_RUNTIME_KERNEL_HEADERS}/${header}")
endforeach()
if(LIBCXX_MMIX_LINUX_STATE_DIR AND NOT LIBCXX_MMIX_LINUX_STATE_DIR STREQUAL "${libc_build}/mmix-state")
  message(FATAL_ERROR "MMIX Linux C++ composition has conflicting state packages")
endif()
set(LIBCXX_MMIX_LINUX_STATE_DIR "${libc_build}/mmix-state")

# Use generated libc headers before kernel headers, never host libc headers.
foreach(language C CXX)
  string(APPEND CMAKE_${language}_FLAGS
    " -nostdlibinc -isystem \"${MMIX_C_RUNTIME_HEADERS}\" -idirafter=${MMIX_C_RUNTIME_KERNEL_HEADERS}")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/InstalledLibc.cmake")
if(NOT RUNTIMES_USE_LIBC STREQUAL "llvm-libc")
  message(FATAL_ERROR "MMIX Linux C++ composition requires LLVM libc")
endif()
target_link_libraries(runtimes-libc-static INTERFACE
  "${MMIX_C_RUNTIME_LIBC}" "${MMIX_C_RUNTIME_STATE}")
foreach(project LIBCXX LIBCXXABI)
  set(${project}_BUILTINS_LIBRARY "${MMIX_C_RUNTIME_BUILTINS}")
endforeach()

# Record selection only. Archive and final-link qualification are separate.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/mmix-linux-runtime")
set(record "# Validated build-tree inputs, not an installed runtime package.\n")
foreach(kind LIBC STATE CRT1 BUILTINS CRTBEGIN CRTEND)
  file(SHA256 "${MMIX_C_RUNTIME_${kind}}" hash)
  string(APPEND record "set(MMIX_CXX_${kind} \"${MMIX_C_RUNTIME_${kind}}\")\n"
    "set(MMIX_CXX_${kind}_SHA256 \"${hash}\")\n")
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/mmix-linux-runtime/Inputs.cmake" "${record}")
