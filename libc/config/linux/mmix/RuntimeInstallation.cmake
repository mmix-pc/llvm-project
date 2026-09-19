option(LIBC_MMIX_INSTALL_RUNTIME
  "Install the static Linux command-environment runtime" OFF)
if(NOT LIBC_MMIX_INSTALL_RUNTIME)
  return()
endif()
if(NOT LIBC_MMIX_BUILD_COMMAND_ENVIRONMENT_RUNTIME OR
   NOT TARGET mmix_libc_command_environment_runtime)
  message(FATAL_ERROR "MMIX Linux installation requires the command-environment runtime")
endif()

# The prefix is already the Linux sysroot's usr directory.
set(LIBC_INSTALL_LIBRARY_DIR lib${LLVM_LIBDIR_SUFFIX})

# The normal archive is created after target configuration. Add only state
# objects that are not already present through entrypoint dependencies.
function(mmix_configure_runtime_installation)
  get_all_object_file_deps(objects "${TARGET_LIBC_ENTRYPOINTS}")
  get_target_property(state_objects mmix_libc_state MMIX_STATE_OBJECT_TARGETS)
  foreach(object IN LISTS state_objects)
    if(NOT object IN_LIST objects)
      target_sources(libc PRIVATE $<TARGET_OBJECTS:${object}>)
    endif()
  endforeach()
  install(TARGETS libc ARCHIVE DESTINATION ${LIBC_INSTALL_LIBRARY_DIR}
    COMPONENT mmix-libc-runtime)
  install(FILES
    $<TARGET_OBJECTS:libc.startup.linux.mmix.crt1>
    ${LIBC_LIBRARY_DIR}/libm.a
    DESTINATION ${LIBC_INSTALL_LIBRARY_DIR} COMPONENT mmix-libc-runtime)
  add_custom_target(mmix_libc_install_runtime
    DEPENDS mmix_libc_command_environment_runtime libc libc-headers)
endfunction()

cmake_language(DEFER CALL mmix_configure_runtime_installation)
