include(${CMAKE_CURRENT_LIST_DIR}/RuntimeMode.cmake)
if(NOT LIBC_MMIX_BUILD_THREAD_LIFECYCLE)
  return()
endif()
mmix_check_runtime_mode()
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.stdlib.atexit
  libc.src.stdlib.exit
  libc.src.unistd.environ
  libc.src.errno.program_invocation_name
  libc.src.errno.program_invocation_short_name
  libc.src.compiler.__stack_chk_fail)
set(TARGET_LLVMLIBC_ENTRYPOINTS ${TARGET_LIBC_ENTRYPOINTS})
if(NOT LLVM_LIBC_FULL_BUILD OR NOT CMAKE_CROSSCOMPILING OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR LLVM_LIBC_INCLUDE_SCUDO OR
   LIBC_CONF_TIMEOUT_ENSURE_MONOTONICITY OR
   NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "mmix")
  message(FATAL_ERROR "MMIX internal lifecycle requires cross Linux Release, full libc and untimed storage without Scudo")
endif()

# Deliberately not libc.a: select real upstream internal fixed-capacity callback
# storage, excluding the public packaging allocator and unqualified consumers.
function(mmix_configure_thread_lifecycle)
  set(mmix_lifecycle_objects
    libc.src.__support.threads.thread
    libc.src.errno.errno
    libc.src.__support.OSUtil.linux.linux_util
    libc.src.__support.OSUtil.linux.mmix.syscall_asm
    libc.src.stdlib.atexit.__internal__
    libc.src.stdlib.exit.__internal__
    libc.src.unistd.environ
    libc.src.errno.program_invocation_name
    libc.src.errno.program_invocation_short_name
    libc.src.compiler.generic.__stack_chk_fail
    libc.src.string.memcpy
    libc.src.string.memmove
    libc.src.string.memset
    libc.src.string.memcmp)
  foreach(provider main_thread tls lifecycle resources thread_entry thread_start
                   thread_create thread_finish thread_reclaim thread_join
                   thread_detach thread_signal thread_reaper_service)
    list(APPEND mmix_lifecycle_objects
      libc.src.__support.threads.linux.mmix.${provider})
  endforeach()
  add_library(mmix_libc_thread_lifecycle STATIC)
  foreach(provider IN LISTS mmix_lifecycle_objects)
    if(NOT TARGET ${provider})
      message(FATAL_ERROR "MMIX internal lifecycle is missing provider ${provider}")
    endif()
    target_sources(mmix_libc_thread_lifecycle PRIVATE $<TARGET_OBJECTS:${provider}>)
  endforeach()
  set_target_properties(mmix_libc_thread_lifecycle PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mmix-thread-lifecycle"
    MMIX_LIFECYCLE_OBJECT_TARGETS "${mmix_lifecycle_objects}")
  add_dependencies(mmix_libc_thread_lifecycle libc.startup.linux.mmix.crt1)
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mmix-thread-lifecycle/Inputs.cmake" CONTENT
  "set(MMIX_LIFECYCLE_ARCHIVE \"$<TARGET_FILE:mmix_libc_thread_lifecycle>\")
  set(MMIX_LIFECYCLE_CRT1 \"$<TARGET_OBJECTS:libc.startup.linux.mmix.crt1>\")
  set(MMIX_LIFECYCLE_PROVIDERS \"${mmix_lifecycle_objects}\")
  set(MMIX_LIFECYCLE_NAMESPACE \"${LIBC_NAMESPACE}\")
  set(MMIX_LIFECYCLE_HEADERS \"${LIBC_INCLUDE_DIR}\")
  set(MMIX_LIFECYCLE_THREAD_MODE \"${LIBC_CONF_THREAD_MODE}\")
  set(MMIX_LIFECYCLE_ERRNO_MODE \"${LIBC_CONF_ERRNO_MODE}\")
  set(MMIX_LIFECYCLE_CALLBACK_CAPACITY 1024)
  set(MMIX_LIFECYCLE_EXCLUDED \"pthread;concurrent-libc;malloc;stdio;TSS;nontrivial-TLS;cancellation;fork;dynamic-linking\")
  ")
  set(manifest "{}")
  foreach(pair "archive|$<TARGET_FILE:mmix_libc_thread_lifecycle>"
               "crt|$<TARGET_OBJECTS:libc.startup.linux.mmix.crt1>"
               "headers|${LIBC_INCLUDE_DIR}" "uapi|${LIBC_KERNEL_HEADERS}"
               "sysroot|${CMAKE_SYSROOT}"
               "namespace|${LIBC_NAMESPACE}")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 key)
    list(GET pair 1 value)
    string(JSON manifest SET "${manifest}" "${key}" "\"${value}\"")
  endforeach()
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mmix-thread-lifecycle/inputs.json"
    CONTENT "${manifest}\n")
endfunction()
cmake_language(DEFER CALL mmix_configure_thread_lifecycle)
