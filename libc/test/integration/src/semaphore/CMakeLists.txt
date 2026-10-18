add_libc_integration_test_suite(libc-semaphore-integration-tests)

add_integration_test(
  signal_test
  SUITE libc-semaphore-integration-tests
  SRCS signal_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.signal_macros
    libc.hdr.time_macros
    libc.src.__support.CPP.atomic
    libc.src.errno.errno
    libc.src.pthread.pthread_create
    libc.src.pthread.pthread_join
    libc.src.semaphore.sem_init
    libc.src.semaphore.sem_destroy
    libc.src.semaphore.sem_getvalue
    libc.src.semaphore.sem_post
    libc.src.semaphore.sem_wait
    libc.src.semaphore.sem_timedwait
    libc.src.signal.pthread_kill
    libc.src.signal.sigaction
    libc.src.signal.sigemptyset
    libc.src.time.clock_gettime
    libc.src.time.nanosleep
)

add_integration_test(
  cancellation_test
  SUITE libc-semaphore-integration-tests
  SRCS cancellation_test.cpp
  DEPENDS
    libc.include.pthread
    libc.hdr.fcntl_macros
    libc.hdr.semaphore_macros
    libc.hdr.stdint_proxy
    libc.hdr.time_macros
    libc.src.__support.CPP.atomic
    libc.src.pthread.pthread_cancel
    libc.src.pthread.pthread_create
    libc.src.pthread.pthread_join
    libc.src.pthread.pthread_self
    libc.src.semaphore.sem_init
    libc.src.semaphore.sem_destroy
    libc.src.semaphore.sem_getvalue
    libc.src.semaphore.sem_open
    libc.src.semaphore.sem_close
    libc.src.semaphore.sem_unlink
    libc.src.semaphore.sem_wait
    libc.src.semaphore.sem_timedwait
    libc.src.sys.mman.shm_open
    libc.src.sys.mman.shm_unlink
    libc.src.unistd.close
    libc.src.time.clock_gettime
  COMPILE_OPTIONS -fexceptions -funwind-tables
)
