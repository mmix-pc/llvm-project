if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include("${CMAKE_CURRENT_LIST_DIR}/Linux.cmake")
  return()
endif()

# MMIX's public profile does not provide clocks, filesystem services, floating
# charconv, or C++23 printing. Do not build them against missing C interfaces.
foreach(feature THREADS FILESYSTEM LOCALIZATION MONOTONIC_CLOCK
                TIME_ZONE_DATABASE RANDOM_DEVICE UNICODE WIDE_CHARACTERS)
  if(LIBCXX_ENABLE_${feature})
    message(FATAL_ERROR "MMIX libc++ requires LIBCXX_ENABLE_${feature}=OFF")
  endif()
endforeach()

list(REMOVE_ITEM LIBCXX_SOURCES
  charconv.cpp
  chrono.cpp
  filesystem/filesystem_clock.cpp
  filesystem/filesystem_error.cpp
  filesystem/path.cpp
  print.cpp
  ryu/d2fixed.cpp
  ryu/d2s.cpp
  ryu/f2s.cpp)
