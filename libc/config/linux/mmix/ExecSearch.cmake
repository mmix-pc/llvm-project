option(LIBC_MMIX_BUILD_EXEC_SEARCH "Build the Linux execvp path-search provider" OFF)
if(LIBC_MMIX_BUILD_EXEC_SEARCH)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.unistd.execvp)
endif()
