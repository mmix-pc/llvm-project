option(LIBC_MMIX_BUILD_USER_ACCOUNTING
  "Build Linux file-backed login accounting interfaces" OFF)
if(NOT LIBC_MMIX_BUILD_USER_ACCOUNTING)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.utmp.setutent
  libc.src.utmp.getutent
  libc.src.utmp.endutent
  libc.src.utmp.getutid
  libc.src.utmp.getutline
  libc.src.utmp.pututline
  libc.src.utmp.utmpname
  libc.src.utmp.updwtmp
  libc.src.utmpx.setutxent
  libc.src.utmpx.getutxent
  libc.src.utmpx.endutxent
  libc.src.utmpx.getutxid
  libc.src.utmpx.getutxline
  libc.src.utmpx.pututxline
  libc.src.utmpx.utmpxname
  libc.src.utmpx.updwtmpx)
