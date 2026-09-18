option(LIBC_MMIX_BUILD_POLL "Build Linux poll and ppoll providers" OFF)
if(LIBC_MMIX_BUILD_POLL)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.poll.poll libc.src.poll.ppoll)
endif()
