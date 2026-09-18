option(LIBC_MMIX_BUILD_PROCESS_TIMING "Build Linux process timing and sleep providers" OFF)
if(LIBC_MMIX_BUILD_PROCESS_TIMING)
  foreach(entry libc.src.sys.times.times libc.src.unistd.alarm
                libc.src.unistd.sleep libc.src.unistd.usleep libc.src.time.nanosleep)
    if(NOT entry IN_LIST TARGET_LIBC_ENTRYPOINTS)
      list(APPEND TARGET_LIBC_ENTRYPOINTS ${entry})
    endif()
  endforeach()
endif()
