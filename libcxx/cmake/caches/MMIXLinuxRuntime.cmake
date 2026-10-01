# Opt-in single-thread build-tree composition; installation is separate.
include("${CMAKE_CURRENT_LIST_DIR}/MMIXLinux.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/MMIXLinuxRuntimeBase.cmake")
set(CMAKE_PROJECT_Runtimes_INCLUDE
  "${CMAKE_CURRENT_LIST_DIR}/../Modules/MMIX/LinuxRuntime.cmake" CACHE FILEPATH "" FORCE)
