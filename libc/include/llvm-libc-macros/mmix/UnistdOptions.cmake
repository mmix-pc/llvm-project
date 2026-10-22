# Public option reporting must follow the provider rather than private client
# compilation flags. A partial pthread preparation is not the public runtime.
function(mmix_set_posix_option option value)
  set(MMIX_POSIX_${option} "(-1L)" PARENT_SCOPE)
  foreach(entry IN LISTS ARGN)
    list(FIND TARGET_LIBC_ENTRYPOINTS "libc.src.${entry}" index)
    if(index EQUAL -1)
      return()
    endif()
  endforeach()
  set(MMIX_POSIX_${option} "${value}" PARENT_SCOPE)
endfunction()

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
  # Query capabilities follow the selected public entrypoints, not client flags.
  mmix_set_posix_option(FSYNC 200809L
    unistd.fsync unistd.fdatasync)
  mmix_set_posix_option(MAPPED_FILES 200809L
    sys.mman.mmap sys.mman.munmap)
  mmix_set_posix_option(MEMORY_PROTECTION 200809L
    sys.mman.mprotect)
  mmix_set_posix_option(MEMLOCK 200809L
    sys.mman.mlockall sys.mman.munlockall)
  mmix_set_posix_option(MEMLOCK_RANGE 200809L
    sys.mman.mlock sys.mman.munlock)
  mmix_set_posix_option(SYNCHRONIZED_IO 200809L
    unistd.fsync unistd.fdatasync)
  mmix_set_posix_option(SEMAPHORES 200809L
    semaphore.sem_init semaphore.sem_open semaphore.sem_wait semaphore.sem_post
    semaphore.sem_close semaphore.sem_unlink semaphore.sem_destroy
    semaphore.sem_getvalue semaphore.sem_trywait semaphore.sem_timedwait)
  mmix_set_posix_option(SHARED_MEMORY_OBJECTS 200809L
    sys.mman.shm_open sys.mman.shm_unlink)
  mmix_set_posix_option(MONOTONIC_CLOCK 200809L
    time.clock_gettime time.clock_getres)
  mmix_set_posix_option(CLOCK_SELECTION 200809L
    time.clock_nanosleep pthread.pthread_condattr_setclock
    pthread.pthread_condattr_getclock)
  mmix_set_posix_option(CPUTIME 200809L
    time.clock_getcpuclockid time.clock_gettime time.clock_getres)
  mmix_set_posix_option(REALTIME_SIGNALS 200809L
    signal.sigqueue signal.sigtimedwait signal.sigwaitinfo)
  mmix_set_posix_option(PRIORITY_SCHEDULING 200809L
    sched.sched_setscheduler sched.sched_getscheduler sched.sched_setparam sched.sched_getparam)
  # FIXME: Qualify the full Spawn option after implementing spawn attributes.
  # Exporting the null-attribute subset does not establish option support.
  set(MMIX_POSIX_SPAWN "(-1L)")
  mmix_set_posix_option(SAVED_IDS 1
    unistd.setuid unistd.setgid)
  mmix_set_posix_option(JOB_CONTROL 1
    unistd.tcsetpgrp unistd.tcgetpgrp unistd.setsid)
  # FIXME: Qualify locale-aware matching and REG_MINIMAL before advertising
  # regular-expression support. The byte-oriented provider is incomplete.
  set(MMIX_POSIX_REGEXP "(-1L)")
  mmix_set_posix_option(TIMEOUTS 200809L
    semaphore.sem_timedwait pthread.pthread_mutex_timedlock
    pthread.pthread_rwlock_timedrdlock pthread.pthread_rwlock_timedwrlock)
  # Robust + PROTECT is rejected by the provider. POSIX/XSI version macros
  # would promise unrelated services and are deliberately not generated.
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/unistd-options.h.in"
    "${output}" @ONLY)
endfunction()
