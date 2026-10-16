include_guard(GLOBAL)

# Keep the bootstrap and full-provider libc links in the same declared graph.
function(mmix_link_shared_libc output inputs builtins abi unwind)
  include("${inputs}")
  if(NOT MMIX_SHARED_LIBC_BUILTINS STREQUAL builtins)
    message(FATAL_ERROR "Shared libc and unwind require the same PIC builtins")
  endif()
  add_custom_command(OUTPUT "${output}"
    COMMAND ${CMAKE_LINKER} -m elf64mmix_linux -shared -soname libc.so.1
      -z now -z relro --hash-style=both --eh-frame-hdr -z defs
      --allow-shlib-undefined --version-script=${MMIX_SHARED_LIBC_EXPORTS}
      --whole-archive "${MMIX_SHARED_LIBC_ARCHIVE}" --no-whole-archive
      ${MMIX_SHARED_LIBC_OBJECTS} "${builtins}" "${abi}" "${unwind}"
      -o "${output}"
    DEPENDS "${MMIX_SHARED_LIBC_ARCHIVE}" ${MMIX_SHARED_LIBC_OBJECTS}
      "${builtins}" "${MMIX_SHARED_LIBC_EXPORTS}" "${abi}" "${unwind}"
      "${inputs}"
    COMMAND_EXPAND_LISTS VERBATIM)
endfunction()
