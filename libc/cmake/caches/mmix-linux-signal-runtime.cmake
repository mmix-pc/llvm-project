# Explicit build-tree preparation; this does not qualify handler execution or
# publish a runtime. Consumers still require reviewed producer/object admission.
include(${CMAKE_CURRENT_LIST_DIR}/mmix-linux-c-runtime.cmake)
set(LIBC_MMIX_BUILD_SIGNAL_RUNTIME ON CACHE BOOL "")
foreach(component SIGNAL_MASKS JUMP_SAVE FORK SPAWN)
  set(LIBC_MMIX_BUILD_${component} ON CACHE BOOL "")
endforeach()
