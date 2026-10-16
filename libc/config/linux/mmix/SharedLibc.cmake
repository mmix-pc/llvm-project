if(NOT LIBC_MMIX_BUILD_SHARED_LIBC)
  return()
endif()
mmix_check_runtime_mode()
foreach(flags CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
    LIBC_COMPILE_OPTIONS_DEFAULT)
  if("${${flags}}" MATCHES "(^|[ ;])(-pg|-finstrument-functions[^ ;]*)([ ;]|$)")
    message(FATAL_ERROR "MMIX shared startup does not support function instrumentation")
  endif()
endforeach()
# Public ABI aliases escape the hidden implementation namespace. Keep the
# declarations consistent in definitions and referencing translation units.
add_compile_definitions(
  LIBC_MMIX_SHARED_STARTUP
  "LLVM_LIBC_FUNCTION_ATTR=__attribute__((visibility(\"default\")))"
  "LLVM_LIBC_VARIABLE_ATTR=__attribute__((visibility(\"default\")))")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include>"
  "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_LIST_DIR}/shared_exports.h>")
set(LIBC_MMIX_SHARED_BUILTINS "" CACHE FILEPATH "Explicit PIC Linux compiler-rt archive")
if(NOT IS_ABSOLUTE "${LIBC_MMIX_SHARED_BUILTINS}" OR
   NOT EXISTS "${LIBC_MMIX_SHARED_BUILTINS}" OR
   IS_DIRECTORY "${LIBC_MMIX_SHARED_BUILTINS}")
  message(FATAL_ERROR "MMIX shared libc requires an explicit PIC builtins archive")
endif()

# Run after the normal threaded archive has acquired its support objects.
function(mmix_configure_shared_libc)
  set(exports __llvm_libc_loader_bootstrap __llvm_libc_mmix_dynamic_start
    __llvm_libc_mmix_retain_exception_images
    __llvm_libc_mmix_release_exception_images
    __tls_get_addr __stack_chk_guard __stack_chk_fail
    __cxa_atexit __cxa_finalize __cxa_thread_atexit_impl
    __llvm_libc_mmix_thread_cleanup_push __llvm_libc_mmix_thread_cleanup_pop
    __llvm_libc_mmix_thread_cleanup_push_cpp)
  foreach(entry IN LISTS mmix_threaded_entrypoints)
    get_target_property(name ${entry} ENTRYPOINT_NAME)
    if(NOT name)
      message(FATAL_ERROR "MMIX shared libc entry lacks its public name: ${entry}")
    endif()
    if(name STREQUAL "errno")
      set(name __llvm_libc_errno)
    endif()
    list(APPEND exports ${name})
  endforeach()
  list(REMOVE_DUPLICATES exports)
  list(SORT exports)
  set(script "{ global:\n")
  foreach(name IN LISTS exports)
    string(APPEND script "  ${name};\n")
  endforeach()
  string(APPEND script "local: *; };\n")
  set(directory "${CMAKE_BINARY_DIR}/mmix-shared-libc")
  get_filename_component(tool_directory "${CMAKE_C_COMPILER}" DIRECTORY)
  set(nm "${tool_directory}/llvm-nm")
  if(NOT EXISTS "${nm}")
    message(FATAL_ERROR "MMIX shared libc requires llvm-nm beside Clang")
  endif()
  file(GENERATE OUTPUT "${directory}/exports.map" CONTENT "${script}")
  file(GENERATE OUTPUT "${directory}/exports.txt" CONTENT "$<JOIN:${exports},\n>\n")
  set(extra_objects
    libc.src.__support.OSUtil.linux.mmix.dynamic_tls
    libc.startup.linux.mmix.shared_prepare
    libc.startup.linux.mmix.shared_adopt
    libc.startup.linux.mmix.shared_run
    libc.startup.linux.mmix.stack_guard)
  set(objects)
  get_all_object_file_deps(archive_objects "${mmix_threaded_entrypoints}")
  foreach(object IN LISTS extra_objects)
    # Shared runtime entrypoints can already pull these support objects into
    # the whole archive (notably the TLS resolver). Link each provider once.
    if(NOT object IN_LIST archive_objects)
      list(APPEND objects $<TARGET_OBJECTS:${object}>)
    endif()
  endforeach()
  # Explicit-input preparation can relink against real resident providers
  # without copying this archive or replacing it with an installed profile.
  file(GENERATE OUTPUT "${directory}/link-inputs.cmake" CONTENT
    "set(MMIX_SHARED_LIBC_ARCHIVE \"$<TARGET_FILE:libc>\")\nset(MMIX_SHARED_LIBC_OBJECTS \"$<JOIN:${objects},;>\")\nset(MMIX_SHARED_LIBC_BUILTINS \"${LIBC_MMIX_SHARED_BUILTINS}\")\nset(MMIX_SHARED_LIBC_EXPORTS \"${directory}/exports.map\")\n")
  # No CRT entry, host library search, interpreter code or duplicate state
  # archive. Internal aliases bind locally; only selected C entrypoints escape.
  add_custom_command(OUTPUT "${directory}/libc.so.1"
    COMMAND ${CMAKE_LINKER} -m elf64mmix_linux -shared -soname libc.so.1
      -z now -z relro --hash-style=both --eh-frame-hdr
      --version-script=${directory}/exports.map
      --whole-archive $<TARGET_FILE:libc> --no-whole-archive
      ${objects} ${LIBC_MMIX_SHARED_BUILTINS}
      -o "${directory}/libc.so.1"
    COMMAND ${CMAKE_COMMAND} -DNM=${nm} -DIMAGE=${directory}/libc.so.1
      -DOUTPUT=${directory}
      -P ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CheckSharedLibc.cmake
    DEPENDS libc ${extra_objects} ${objects} "${directory}/exports.map"
      "${LIBC_MMIX_SHARED_BUILTINS}"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CheckSharedLibc.cmake"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shared-unwind-imports.txt"
    COMMAND_EXPAND_LISTS VERBATIM)
  add_custom_target(mmix_libc_shared DEPENDS "${directory}/libc.so.1")
endfunction()
cmake_language(DEFER CALL mmix_configure_shared_libc)
