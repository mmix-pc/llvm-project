option(LIBC_MMIX_BUILD_SYSTEM_ADMINISTRATION
  "Build Linux mount, reboot, uname and sync providers" OFF)
if(LIBC_MMIX_BUILD_SYSTEM_ADMINISTRATION)
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.sys.mount.mount
    libc.src.sys.mount.umount
    libc.src.sys.mount.umount2
    libc.src.sys.reboot.reboot
    libc.src.sys.utsname.uname
    libc.src.unistd.sync)
endif()
