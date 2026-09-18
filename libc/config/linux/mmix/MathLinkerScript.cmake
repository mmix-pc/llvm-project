if(NOT LIBC_MMIX_BUILD_MATH OR NOT LLVM_LIBC_FULL_BUILD)
  return()
endif()

# Keep explicit -lm usable without duplicating the selected math providers.
configure_file(${CMAKE_CURRENT_LIST_DIR}/libm.ld
  ${LIBC_LIBRARY_DIR}/libm.a COPYONLY)
install(FILES ${LIBC_LIBRARY_DIR}/libm.a
  DESTINATION ${LIBC_INSTALL_LIBRARY_DIR}
  COMPONENT libc)
