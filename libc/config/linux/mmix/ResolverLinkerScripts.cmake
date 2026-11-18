if(NOT LIBC_MMIX_BUILD_NAME_RESOLUTION OR NOT LLVM_LIBC_FULL_BUILD)
  return()
endif()

# Resolver packet interfaces are part of the selected libc, not another DSO.
configure_file(${CMAKE_CURRENT_LIST_DIR}/libresolv-static.ld
  ${LIBC_LIBRARY_DIR}/libresolv.a COPYONLY)
set(resolver_link_files ${LIBC_LIBRARY_DIR}/libresolv.a)
if(LIBC_MMIX_BUILD_SHARED_LIBC)
  configure_file(${CMAKE_CURRENT_LIST_DIR}/libresolv-shared.ld
    ${LIBC_LIBRARY_DIR}/libresolv.so COPYONLY)
  list(APPEND resolver_link_files ${LIBC_LIBRARY_DIR}/libresolv.so)
endif()
install(FILES ${resolver_link_files}
  DESTINATION ${LIBC_INSTALL_LIBRARY_DIR} COMPONENT libc)
if(LIBC_MMIX_INSTALL_RUNTIME)
  install(FILES ${resolver_link_files}
    DESTINATION lib${LLVM_LIBDIR_SUFFIX} COMPONENT mmix-libc-runtime)
endif()
if(LIBC_MMIX_INSTALL_THREADED_RUNTIME)
  install(FILES ${resolver_link_files}
    DESTINATION lib${LLVM_LIBDIR_SUFFIX} COMPONENT mmix-libc-threaded-runtime)
endif()
