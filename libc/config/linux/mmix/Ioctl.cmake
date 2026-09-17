option(LIBC_MMIX_BUILD_IOCTL "Build the Linux ioctl provider" OFF)
if(LIBC_MMIX_BUILD_IOCTL)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.sys.ioctl.ioctl)
endif()
