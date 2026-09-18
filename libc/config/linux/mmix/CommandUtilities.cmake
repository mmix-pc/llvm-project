option(LIBC_MMIX_BUILD_COMMAND_UTILITIES
  "Build Linux command text, environment, stdio and path providers" OFF)
if(NOT LIBC_MMIX_BUILD_COMMAND_UTILITIES)
  return()
endif()
