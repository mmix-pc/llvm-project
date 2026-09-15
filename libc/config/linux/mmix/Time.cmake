option(LIBC_MMIX_BUILD_TIME "Build Linux time providers" OFF)
if(NOT LIBC_MMIX_BUILD_TIME)
  return()
endif()

set(MMIX_TIME_ENTRYPOINTS
  libc.src.time.clock_gettime
  libc.src.time.time
  libc.src.time.timespec_get
  libc.src.time.gmtime
  libc.src.time.gmtime_r
  libc.src.time.localtime
  libc.src.time.localtime_r
  libc.src.time.mktime
  libc.src.time.difftime
  libc.src.time.asctime
  libc.src.time.asctime_r
  libc.src.time.ctime
  libc.src.time.ctime_r
  libc.src.time.strftime
  libc.src.sys.time.gettimeofday
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_TIME_ENTRYPOINTS})
