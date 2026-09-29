include(${CMAKE_CURRENT_LIST_DIR}/../../../../config/linux/mmix/RuntimeMode.cmake)
mmix_check_runtime_mode()
if(LLVM_LIBC_INCLUDE_SCUDO OR NOT LLVM_LIBC_FULL_BUILD)
  message(FATAL_ERROR "MMIX Linux allocation requires full LLVM libc without Scudo")
endif()

add_subdirectory(linux/mmix)
foreach(entrypoint malloc free calloc realloc aligned_alloc posix_memalign)
  add_entrypoint_object(${entrypoint}
    ALIAS
    DEPENDS .linux.mmix.${entrypoint})
endforeach()

# Allocator tuning is outside the selected provider contract.
add_entrypoint_external(mallopt)
