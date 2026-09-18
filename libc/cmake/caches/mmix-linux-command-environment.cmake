# Build-tree command-environment composition, not application or kernel qualification.
include(${CMAKE_CURRENT_LIST_DIR}/mmix-linux-signal-runtime.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../../config/linux/mmix/CommandEnvironmentComponents.cmake)
set(LIBC_MMIX_BUILD_COMMAND_ENVIRONMENT_RUNTIME ON CACHE BOOL "")
set(LIBC_MMIX_ENABLE_UNIX_HEADERS ON CACHE BOOL "")
foreach(component IN LISTS MMIX_COMMAND_ENVIRONMENT_COMPONENTS)
  set(LIBC_MMIX_BUILD_${component} ON CACHE BOOL "")
endforeach()
