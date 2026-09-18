option(LIBC_MMIX_BUILD_COMMAND_UTILITIES
  "Build Linux command text, environment, stdio and path providers" OFF)
if(NOT LIBC_MMIX_BUILD_COMMAND_UTILITIES)
  return()
endif()
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.stdlib.clearenv
  libc.src.stdlib.mkstemp
  libc.src.libgen.dirname
  libc.src.string.strverscmp
  libc.src.stdio.fseeko)
