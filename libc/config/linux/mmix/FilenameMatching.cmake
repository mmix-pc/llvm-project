option(LIBC_MMIX_BUILD_FNMATCH "Build filename pattern matching" OFF)
if(LIBC_MMIX_BUILD_FNMATCH)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.fnmatch.fnmatch)
endif()
