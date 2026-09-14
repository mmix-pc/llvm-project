# Keep declaration-only preparation out of published runtime resources.
if(NOT RUNTIMES_USE_LIBC STREQUAL "llvm-libc" OR
   NOT LIBCXX_ENABLE_THREADS OR NOT LIBCXX_HAS_EXTERNAL_THREAD_API OR
   LIBCXX_HAS_PTHREAD_API OR LIBCXX_HAS_C11_THREAD_API OR
   LIBCXX_HAS_WIN32_THREAD_API OR LIBCXX_ENABLE_SHARED OR
   LIBCXX_INSTALL_HEADERS OR LIBCXX_INSTALL_LIBRARY)
  message(FATAL_ERROR "MMIX Linux libc++ requires non-installing static external-thread preparation with LLVM libc")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/linux/__external_threading"
  "${LIBCXX_GENERATED_INCLUDE_TARGET_DIR}/__external_threading" COPYONLY)
