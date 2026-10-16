# Component installation is explicit; preparation alone does not publish a profile.
include_guard(GLOBAL)

function(mmix_install_shared_library image name component)
  set(directory "${CMAKE_CURRENT_BINARY_DIR}/mmix-install-links/${component}")
  file(MAKE_DIRECTORY "${directory}")
  file(CREATE_LINK "${name}.so.1" "${directory}/${name}.so" SYMBOLIC)
  install(FILES "${image}" DESTINATION lib RENAME "${name}.so.1"
    COMPONENT ${component} EXCLUDE_FROM_ALL)
  install(FILES "${directory}/${name}.so" DESTINATION lib
    COMPONENT ${component} EXCLUDE_FROM_ALL)
endfunction()

function(mmix_install_shared_profile name value component)
  set(path "${CMAKE_CURRENT_BINARY_DIR}/mmix-install-links/${name}")
  file(WRITE "${path}" "${value}\n")
  install(FILES "${path}" DESTINATION lib COMPONENT ${component}
    EXCLUDE_FROM_ALL)
endfunction()

function(mmix_shared_installation)
  mmix_install_shared_library("$<TARGET_FILE:cxx_shared>" libc++ mmix-shared-cxx)
  mmix_install_shared_library("$<TARGET_FILE:cxxabi_shared>" libc++abi mmix-shared-cxx)
  mmix_install_shared_library("$<TARGET_FILE:unwind_shared>" libunwind mmix-shared-cxx)
  mmix_install_shared_profile(mmix-shared-cxx-profile
    mmix-linux-shared-pthread-cxx mmix-shared-cxx)
endfunction()
