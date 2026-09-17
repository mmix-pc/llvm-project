# Build-tree composition of the available Linux C providers, not a sysroot
# publication. Signal-dependent fork/spawn remain separate prerequisites.
include(${CMAKE_CURRENT_LIST_DIR}/mmix-linux.cmake)
set(LIBC_MMIX_BUILD_C_RUNTIME ON CACHE BOOL "")
foreach(component CORE DESCRIPTORS STDIO WIDE FILESYSTEM VM TIME PROCESS_BASE
                  RUNTIME_STATE ALLOCATION)
  set(LIBC_MMIX_BUILD_${component} ON CACHE BOOL "")
endforeach()
