# Keep the retained SINGLE and Generic profiles on their existing source lists.
option(LIBCXXABI_MMIX_LINUX_CANCELLATION_SAFE_GUARDS
  "Use noncanceling futex waits for MMIX Linux C++ initialization guards" OFF)
if(LIBCXXABI_MMIX_LINUX_CANCELLATION_SAFE_GUARDS AND
   NOT LIBCXXABI_MMIX_LINUX_TLS_DESTRUCTORS)
  message(FATAL_ERROR "MMIX cancellation-safe guards require the threaded Linux runtime")
endif()
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
if(LIBCXXABI_MMIX_LINUX_CANCELLATION_SAFE_GUARDS)
  # Clang's guard-acquire calls cannot unwind. The existing raw futex backend
  # preserves pending cancellation without entering a public condition wait.
  set_property(SOURCE cxa_guard.cpp APPEND PROPERTY
    COMPILE_DEFINITIONS _LIBCXXABI_USE_FUTEX)
endif()
