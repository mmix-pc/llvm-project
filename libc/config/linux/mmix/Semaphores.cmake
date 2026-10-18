option(LIBC_MMIX_BUILD_SEMAPHORES "Build Linux POSIX semaphore providers" OFF)
if(NOT LIBC_MMIX_BUILD_SEMAPHORES)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.semaphore.sem_init
  libc.src.semaphore.sem_destroy
  libc.src.semaphore.sem_getvalue
  libc.src.semaphore.sem_post
  libc.src.semaphore.sem_wait
  libc.src.semaphore.sem_trywait
  libc.src.semaphore.sem_timedwait
  libc.src.semaphore.sem_clockwait
  libc.src.semaphore.sem_open
  libc.src.semaphore.sem_close
  libc.src.semaphore.sem_unlink)
