# Compose build-tree preparation only; full C++ runtime closure is separate.
foreach(project LIBCXXABI LIBUNWIND)
  if(${project}_ENABLE_THREADS OR ${project}_ENABLE_SHARED OR
     NOT ${project}_ENABLE_STATIC OR ${project}_INSTALL_HEADERS OR
     ${project}_INSTALL_LIBRARY OR NOT ${project}_USE_COMPILER_RT OR
     ${project}_HAS_PTHREAD_LIB OR ${project}_HAS_DL_LIB)
    message(FATAL_ERROR "MMIX Linux state requires non-installing static single-thread ${project}")
  endif()
endforeach()
if(NOT LIBCXX_CXX_ABI STREQUAL "libcxxabi" OR
   NOT LIBCXX_ENABLE_EXCEPTIONS OR NOT LIBCXXABI_ENABLE_EXCEPTIONS OR
   NOT LIBCXXABI_USE_LLVM_UNWINDER OR LIBCXXABI_HAS_EXTERNAL_THREAD_API OR
   LIBCXXABI_HAS_PTHREAD_API OR LIBCXXABI_HAS_WIN32_THREAD_API OR
   LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL OR LIBCXXABI_HAS_GCC_S_LIB OR
   LIBUNWIND_USE_FRAME_HEADER_CACHE OR LIBCXXABI_BAREMETAL OR LIBUNWIND_IS_BAREMETAL OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR NOT CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "MMIX Linux state requires cross Release with exceptions and process-global ABI state")
endif()
include("${LIBCXX_MMIX_LINUX_STATE_DIR}/LibcStateConfig.cmake")
get_target_property(state_archive mmix_libc_state IMPORTED_LOCATION_RELEASE)
if(NOT EXISTS "${state_archive}" OR IS_DIRECTORY "${state_archive}")
  message(FATAL_ERROR "Build the MMIX Linux libc state archive before composing libc++ primitives")
endif()
get_filename_component(state_sysroot "${MMIX_STATE_SYSROOT}" REALPATH)
get_filename_component(selected_sysroot "${CMAKE_SYSROOT}" REALPATH)
get_filename_component(state_compiler "${MMIX_STATE_COMPILER}" REALPATH)
get_filename_component(selected_compiler "${CMAKE_CXX_COMPILER}" REALPATH)
get_filename_component(state_source "${MMIX_STATE_SOURCE}" REALPATH)
get_filename_component(selected_source "${LIBCXX_SOURCE_DIR}/../libc" REALPATH)
if(NOT state_sysroot STREQUAL selected_sysroot OR
   NOT state_compiler STREQUAL selected_compiler OR
   NOT state_source STREQUAL selected_source OR
   NOT EXISTS "${MMIX_STATE_HEADERS}/sys/syscall.h")
  message(FATAL_ERROR "MMIX Linux state provider must match the compiler, source and sysroot with generated headers")
endif()
foreach(target cxx_mmix_sync cxx_mmix_thread_api)
  target_include_directories(${target} SYSTEM PRIVATE "${MMIX_STATE_HEADERS}")
  target_compile_options(${target} PRIVATE -nostdlibinc
    "-idirafter=${MMIX_STATE_KERNEL_HEADERS}")
endforeach()
add_library(cxx_mmix_runtime_state STATIC
  $<TARGET_OBJECTS:cxx_mmix_sync> $<TARGET_OBJECTS:cxx_mmix_thread_api>)
target_link_libraries(cxx_mmix_runtime_state PUBLIC mmix_libc_state)
set_target_properties(cxx_mmix_runtime_state PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mmix-state")
