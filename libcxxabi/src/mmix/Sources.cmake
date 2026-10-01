# Keep the retained SINGLE and Generic profiles on their existing source lists.
if(NOT LIBCXXABI_MMIX_LINUX_TLS_DESTRUCTORS)
  return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT LIBCXXABI_ENABLE_THREADS OR NOT LIBCXXABI_HAS_CXA_THREAD_ATEXIT_IMPL OR
   LIBCXXABI_ENABLE_SHARED OR
   NOT MMIX_LINUX_THREADED_TLS_DESTRUCTORS_VALIDATED)
  message(FATAL_ERROR "MMIX TLS registration requires matched static threaded libc providers")
endif()
list(REMOVE_ITEM LIBCXXABI_SOURCES cxa_thread_atexit.cpp)
list(APPEND LIBCXXABI_SOURCES mmix/cxa_thread_atexit.cpp)
# Registration calls only the nonthrowing libc provider, never user callbacks.
# Do not pull exception globals or a personality into this C ABI leaf.
set_source_files_properties(mmix/cxa_thread_atexit.cpp PROPERTIES
  COMPILE_OPTIONS -fno-exceptions)
