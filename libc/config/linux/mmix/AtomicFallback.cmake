# Linux's canonical fallback state lives in libc, never in individual DSOs.
# Reuse compiler-rt's algorithm from the matching monorepo source tree.
function(mmix_configure_atomic_fallback)
  set(mmix_atomic_source
    "${LIBC_SOURCE_DIR}/../compiler-rt/lib/builtins/mmix/linux_atomic.c")
  if(NOT EXISTS "${mmix_atomic_source}")
    message(FATAL_ERROR "MMIX Linux libc requires matching compiler-rt atomic sources")
  endif()
  add_library(mmix_linux_atomic OBJECT "${mmix_atomic_source}")
  set_target_properties(mmix_linux_atomic PROPERTIES C_STANDARD 11)
  target_compile_options(mmix_linux_atomic PRIVATE
    -ffreestanding -fno-builtin -fPIC -fvisibility=default
    -fno-lto -fno-stack-protector)
endfunction()

set(mmix_atomic_exports __atomic_is_lock_free __atomic_load __atomic_store
  __atomic_exchange __atomic_compare_exchange)
foreach(size 1 2 4 8)
  foreach(operation load store exchange compare_exchange
      fetch_add fetch_sub fetch_and fetch_or fetch_xor fetch_nand)
    list(APPEND mmix_atomic_exports __atomic_${operation}_${size})
  endforeach()
endforeach()
