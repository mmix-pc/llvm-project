option(LIBC_MMIX_BUILD_FILE_METADATA "Build Linux path timestamp and device-number providers" OFF)
if(LIBC_MMIX_BUILD_FILE_METADATA)
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.sys.stat.utimensat
    libc.src.sys.sysmacros.major
    libc.src.sys.sysmacros.minor
    libc.src.sys.sysmacros.makedev)
endif()
