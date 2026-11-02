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
  # Publish the accepted Linux process providers, not the SINGLE null-attribute
  # spawn subset or an internal preparation archive. Keep this prerequisite
  # closure aligned with RuntimeMode.cmake's full extended/coordinated profile;
  # the existing thread and unrelated option predicates above are unchanged.
  set(mmix_process_options TRUE)
  foreach(component THREADED_LIBC THREAD_LIFECYCLE RUNTIME_STATE
                    PTHREAD_CREATION PTHREAD_LIFECYCLE PTHREAD_MUTEX
                    PTHREAD_COND PTHREAD_ONCE PTHREAD_TSS PTHREAD_SIGNALS
                    CONCURRENT_ALLOCATION CONCURRENT_STDIO EXTENDED_PTHREAD
                    MUTEX_TYPES TIMED_MUTEX PTHREAD_RWLOCK PTHREAD_BARRIER
                    PTHREAD_SPIN PROCESS_SHARED ROBUST_MUTEX PI_MUTEX
                    PROTECT_MUTEX PTHREAD_SCHEDULING PTHREAD_CLOCKS
                    CONCURRENCY_HINTS COORDINATED_FORK UNWIND_DIAGNOSTICS
                    UNWIND_ROOTS TLS_DESTRUCTORS CXX_RUNTIME_DEPENDENCIES
                    FORCED_EXIT DEFERRED_CANCELLATION CANCELLATION_WINDOWS
                    CANCELLATION_RUNTIME_LOCKS ASYNC_CANCELLATION)
    if(NOT LIBC_MMIX_BUILD_${component})
      set(mmix_process_options FALSE)
    endif()
  endforeach()
  if(NOT LIBC_TARGET_OS STREQUAL "linux" OR
     NOT LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" OR
     NOT LLVM_LIBC_FULL_BUILD OR NOT LIBC_MMIX_ENABLE_STATIC_TLS OR
     LIBC_MMIX_BUILD_SINGLE_THREADED_LIBC OR
     NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM" OR
     NOT LIBC_CONF_ERRNO_MODE STREQUAL "LIBC_ERRNO_MODE_THREAD_LOCAL" OR
     (LIBC_MMIX_BUILD_SHARED_LIBC AND NOT CMAKE_POSITION_INDEPENDENT_CODE))
    set(mmix_process_options FALSE)
  endif()
  set(MMIX_POSIX_SPAWN "(-1L)")
  set(MMIX_POSIX_ASYNCHRONOUS_IO "(-1L)")
  if(mmix_process_options)
    # Scheduling attributes use the internal kernel transport. They require
    # the selected scheduling runtime, not exported public sched_* wrappers or
    # a positive whole-process PRIORITY_SCHEDULING advertisement.
    mmix_set_posix_option(SPAWN 202405L
      spawn.posix_spawn spawn.posix_spawnp
      spawn.posix_spawn_file_actions_init spawn.posix_spawn_file_actions_destroy
      spawn.posix_spawn_file_actions_addopen spawn.posix_spawn_file_actions_addclose
      spawn.posix_spawn_file_actions_adddup2 spawn.posix_spawn_file_actions_addchdir
      spawn.posix_spawn_file_actions_addfchdir
      spawn.posix_spawnattr_init spawn.posix_spawnattr_destroy
      spawn.posix_spawnattr_getflags spawn.posix_spawnattr_setflags
      spawn.posix_spawnattr_getpgroup spawn.posix_spawnattr_setpgroup
      spawn.posix_spawnattr_getschedparam spawn.posix_spawnattr_setschedparam
      spawn.posix_spawnattr_getschedpolicy spawn.posix_spawnattr_setschedpolicy
      spawn.posix_spawnattr_getsigdefault spawn.posix_spawnattr_setsigdefault
      spawn.posix_spawnattr_getsigmask spawn.posix_spawnattr_setsigmask)
    # The eight entrypoints alone do not select the matched cancellation,
    # notification and io_uring service. AIO and signal cancellation stay opt-in;
    # this accepted non-PIO provider never advertises PRIORITIZED_IO.
    if(LIBC_MMIX_BUILD_AIO AND LIBC_MMIX_BUILD_SIGNAL_CANCELLATION AND
       LIBC_MMIX_BUILD_JUMP_SAVE)
      mmix_set_posix_option(ASYNCHRONOUS_IO 202405L
        aio.aio_read aio.aio_write aio.aio_fsync aio.aio_error aio.aio_return
        aio.aio_cancel aio.aio_suspend aio.lio_listio)
    endif()
  endif()
  mmix_set_posix_option(SAVED_IDS 1
    unistd.setuid unistd.setgid)
  mmix_set_posix_option(JOB_CONTROL 1
    unistd.tcsetpgrp unistd.tcgetpgrp unistd.setsid)
  mmix_set_posix_option(REGEXP 1
    regex.regcomp regex.regexec regex.regerror regex.regfree)
  mmix_set_posix_option(TIMEOUTS 200809L
    semaphore.sem_timedwait pthread.pthread_mutex_timedlock
    pthread.pthread_rwlock_timedrdlock pthread.pthread_rwlock_timedwrlock)
  # Robust + PROTECT is rejected by the provider. POSIX/XSI version macros
  # would promise unrelated services and are deliberately not generated.
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/unistd-options.h.in"
    "${output}" @ONLY)
endfunction()
