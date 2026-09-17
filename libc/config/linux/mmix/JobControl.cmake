option(LIBC_MMIX_BUILD_JOB_CONTROL "Build Linux session and terminal job-control providers" OFF)
if(LIBC_MMIX_BUILD_JOB_CONTROL)
  set(MMIX_JOB_CONTROL_ENTRYPOINTS
    libc.src.unistd.getpgid
    libc.src.unistd.getpgrp
    libc.src.unistd.setpgid
    libc.src.unistd.getsid
    libc.src.unistd.setsid
    libc.src.unistd.tcgetpgrp
    libc.src.unistd.tcsetpgrp
    libc.src.unistd.isatty
    libc.src.unistd.ttyname
    libc.src.unistd.ttyname_r)
  foreach(entry IN LISTS MMIX_JOB_CONTROL_ENTRYPOINTS)
    if(NOT entry IN_LIST TARGET_LIBC_ENTRYPOINTS)
      list(APPEND TARGET_LIBC_ENTRYPOINTS ${entry})
    endif()
  endforeach()
endif()
