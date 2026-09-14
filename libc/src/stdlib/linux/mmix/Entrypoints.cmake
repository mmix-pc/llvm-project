if(LLVM_LIBC_INCLUDE_SCUDO OR NOT LLVM_LIBC_FULL_BUILD OR
   NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE" OR
   NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_SHARED")
  message(FATAL_ERROR "MMIX Linux allocation requires full LLVM libc with SINGLE mode and shared errno, without Scudo")
endif()

add_subdirectory(linux/mmix)
foreach(entrypoint malloc free calloc realloc aligned_alloc posix_memalign)
  add_entrypoint_object(${entrypoint}
    ALIAS
    DEPENDS .linux.mmix.${entrypoint})
endforeach()

# Allocator tuning is outside the selected provider contract.
add_entrypoint_external(mallopt)
