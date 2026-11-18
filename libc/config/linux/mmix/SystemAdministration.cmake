option(LIBC_MMIX_BUILD_SYSTEM_ADMINISTRATION
  "Build Linux mount, reboot, uname and sync providers" OFF)
if(LIBC_MMIX_BUILD_SYSTEM_ADMINISTRATION)
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.sys.capability.capget
    libc.src.sys.capability.capset
    libc.src.sys.mount.pivot_root
    libc.src.sys.sysinfo.sysinfo
    libc.src.sys.swap.swapoff
    libc.src.sys.swap.swapon
    libc.src.unistd.acct
    libc.src.sys.klog.klogctl
    libc.src.sys.timex.adjtimex
    libc.src.sys.timex.ntp_adjtime
    libc.src.unistd.vhangup
    libc.src.unistd.sethostname
    libc.src.sched.setns
    libc.src.sched.unshare
    libc.src.sys.personality.personality
    libc.src.sys.mount.mount
    libc.src.sys.mount.umount
    libc.src.sys.mount.umount2
    libc.src.sys.reboot.reboot
    libc.src.sys.utsname.uname
    libc.src.unistd.sync)
endif()
