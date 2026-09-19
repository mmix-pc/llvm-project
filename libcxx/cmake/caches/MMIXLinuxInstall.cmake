# Install the validated static Linux composition through native components.
set(LIBCXX_MMIX_LINUX_INSTALL_RUNTIME ON CACHE BOOL "")
include("${CMAKE_CURRENT_LIST_DIR}/MMIXLinuxRuntime.cmake")
foreach(project LIBCXX LIBCXXABI LIBUNWIND)
  set(${project}_INSTALL_HEADERS ON CACHE BOOL "" FORCE)
  set(${project}_INSTALL_LIBRARY ON CACHE BOOL "" FORCE)
endforeach()
set(LIBUNWIND_INSTALL_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
