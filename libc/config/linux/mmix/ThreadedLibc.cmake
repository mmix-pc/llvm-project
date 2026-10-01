if(NOT LIBC_MMIX_BUILD_THREADED_LIBC)
  return()
endif()
mmix_check_runtime_mode()

# The Linux CRT owns the AT_RANDOM guard and its failure entrypoint. The
# internal archive's fixed-guard fallback must not enter this composition.
list(REMOVE_ITEM TARGET_LIBC_ENTRYPOINTS libc.src.compiler.__stack_chk_fail)

# Select individual reviewed providers, not whole optional component families.
# Fork and credential wrappers retain pre-creation-only runtime admission.
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.unistd.fork
  libc.src.unistd.vfork
  libc.src.spawn.posix_spawn
  libc.src.unistd.setuid
  libc.src.unistd.setgid
  libc.src.unistd.seteuid
  libc.src.unistd.setegid
  libc.src.stdlib.getenv
  libc.src.stdlib.rand
  libc.src.stdlib.srand
  libc.src.string.strtok
  libc.src.string.strtok_r
  libc.src.string.strerror
  libc.src.string.strsignal
  libc.src.locale.setlocale
  libc.src.locale.localeconv
  libc.src.time.localtime
  libc.src.time.localtime_r
  libc.src.time.ctime_r
  libc.src.time.asctime_r
  libc.src.signal.sigaction
  libc.src.signal.sigemptyset
  libc.src.signal.sigaddset
)
list(REMOVE_DUPLICATES TARGET_LIBC_ENTRYPOINTS)
set(TARGET_LLVMLIBC_ENTRYPOINTS ${TARGET_LIBC_ENTRYPOINTS})
set(mmix_threaded_entrypoints ${TARGET_LIBC_ENTRYPOINTS})

function(mmix_configure_threaded_libc)
  get_all_object_file_deps(public_objects "${mmix_threaded_entrypoints}")
  get_property(internal_objects DIRECTORY PROPERTY MMIX_THREAD_SUPPORT_OBJECTS)
  # Reuse the real internal support graph, never its private entrypoint variants
  # (notably the fixed-capacity atexit implementation).
  set(support_objects "")
  foreach(provider IN LISTS internal_objects)
    get_target_property(kind ${provider} TARGET_TYPE)
    if(kind STREQUAL OBJECT_LIBRARY)
      collect_object_file_deps(${provider} dependencies)
      list(APPEND support_objects ${dependencies})
    endif()
  endforeach()
  collect_object_file_deps(
    libc.src.__support.threads.linux.mmix.process_operation dependencies)
  list(APPEND support_objects ${dependencies})
  list(REMOVE_DUPLICATES support_objects)
  foreach(provider IN LISTS support_objects)
    if(NOT provider IN_LIST public_objects)
      target_sources(libc PRIVATE $<TARGET_OBJECTS:${provider}>)
      list(APPEND public_objects ${provider})
    endif()
  endforeach()
  foreach(provider IN LISTS public_objects)
    if(NOT TARGET ${provider})
      message(FATAL_ERROR "MMIX threaded libc is missing provider ${provider}")
    endif()
  endforeach()
  add_custom_target(mmix_libc_threaded
    DEPENDS libc libc-headers libc.startup.linux.mmix.crt1)
  set(manifest "{}")
  foreach(pair "archive|$<TARGET_FILE:libc>"
               "crt|$<TARGET_OBJECTS:libc.startup.linux.mmix.crt1>"
               "headers|${LIBC_INCLUDE_DIR}" "uapi|${LIBC_KERNEL_HEADERS}"
               "sysroot|${CMAKE_SYSROOT}" "namespace|${LIBC_NAMESPACE}"
               "thread_mode|${LIBC_CONF_THREAD_MODE}"
               "errno_mode|${LIBC_CONF_ERRNO_MODE}")
    string(REPLACE "|" ";" pair "${pair}")
    list(GET pair 0 key)
    list(GET pair 1 value)
    string(JSON manifest SET "${manifest}" "${key}" "\"${value}\"")
  endforeach()
  foreach(kind entrypoints objects)
    if(kind STREQUAL entrypoints)
      set(values ${mmix_threaded_entrypoints})
    else()
      set(values ${public_objects})
    endif()
    set(array "[]")
    set(index 0)
    foreach(value IN LISTS values)
      string(JSON array SET "${array}" ${index} "\"${value}\"")
      math(EXPR index "${index}+1")
    endforeach()
    string(JSON manifest SET "${manifest}" "${kind}" "${array}")
  endforeach()
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/mmix-threaded-libc/inputs.json"
    CONTENT "${manifest}\n")
endfunction()
cmake_language(DEFER CALL mmix_configure_threaded_libc)
