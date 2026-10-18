# Public option reporting must follow the provider rather than private client
# compilation flags. A partial pthread preparation is not the public runtime.
function(mmix_configure_unistd_options output)
  set(options THREADS THREAD_ATTR_STACKADDR THREAD_ATTR_STACKSIZE
    THREAD_PRIORITY_SCHEDULING THREAD_PRIO_INHERIT THREAD_PRIO_PROTECT
    THREAD_PROCESS_SHARED THREAD_CPUTIME THREAD_ROBUST_PRIO_INHERIT
    THREAD_ROBUST_PRIO_PROTECT BARRIERS READER_WRITER_LOCKS SPIN_LOCKS)
  foreach(option IN LISTS options)
    set(MMIX_POSIX_${option} "(-1L)")
  endforeach()
  if(LIBC_MMIX_BUILD_THREADED_LIBC AND LIBC_MMIX_BUILD_THREAD_LIFECYCLE
      AND LIBC_MMIX_BUILD_PTHREAD_CREATION AND LIBC_MMIX_BUILD_PTHREAD_LIFECYCLE
      AND LIBC_MMIX_BUILD_PTHREAD_MUTEX AND LIBC_MMIX_BUILD_PTHREAD_COND
      AND LIBC_MMIX_BUILD_PTHREAD_ONCE AND LIBC_MMIX_BUILD_PTHREAD_TSS
      AND LIBC_MMIX_BUILD_PTHREAD_SIGNALS AND LIBC_MMIX_BUILD_DEFERRED_CANCELLATION
      AND LIBC_MMIX_BUILD_ASYNC_CANCELLATION AND LIBC_MMIX_BUILD_COORDINATED_FORK)
    foreach(option THREADS THREAD_ATTR_STACKADDR THREAD_ATTR_STACKSIZE)
      set(MMIX_POSIX_${option} "200809L")
    endforeach()
    foreach(pair "PTHREAD_SCHEDULING|THREAD_PRIORITY_SCHEDULING"
                 "PI_MUTEX|THREAD_PRIO_INHERIT"
                 "PROTECT_MUTEX|THREAD_PRIO_PROTECT"
                 "PROCESS_SHARED|THREAD_PROCESS_SHARED"
                 "PTHREAD_CLOCKS|THREAD_CPUTIME"
                 "PTHREAD_BARRIER|BARRIERS"
                 "PTHREAD_RWLOCK|READER_WRITER_LOCKS"
                 "PTHREAD_SPIN|SPIN_LOCKS")
      string(REPLACE "|" ";" fields "${pair}")
      list(GET fields 0 component)
      list(GET fields 1 option)
      if(LIBC_MMIX_BUILD_${component})
        set(MMIX_POSIX_${option} "200809L")
      endif()
    endforeach()
    if(LIBC_MMIX_BUILD_ROBUST_MUTEX AND LIBC_MMIX_BUILD_PI_MUTEX)
      set(MMIX_POSIX_THREAD_ROBUST_PRIO_INHERIT "200809L")
    endif()
  endif()
  # Robust + PROTECT is rejected by the provider. POSIX/XSI version macros
  # would promise unrelated services and are deliberately not generated.
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/unistd-options.h.in"
    "${output}" @ONLY)
endfunction()
