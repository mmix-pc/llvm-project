add_custom_target(libc_signal_unittests)

if(LIBC_TARGET_OS STREQUAL "linux")
  if(LLVM_LIBC_FULL_BUILD)
    add_libc_test(
      sigsuspend_test
      SUITE libc_signal_unittests
      SRCS
        sigsuspend_test.cpp
        ${LIBC_SOURCE_DIR}/src/signal/linux/sigsuspend.cpp
        ${LIBC_SOURCE_DIR}/src/signal/linux/sigismember.cpp
      DEPENDS
        libc.hdr.signal_macros
        libc.hdr.types.sigset_t
        libc.include.signal
        libc.include.sys_syscall
        libc.src.__support.OSUtil.osutil
        libc.src.errno.errno
        libc.src.signal.raise
        libc.src.signal.sigaction
        libc.src.signal.sigaddset
        libc.src.signal.sigemptyset
        libc.src.signal.sigprocmask
    )
  endif()

  add_libc_test(
    raise_error_test
    SUITE libc_signal_unittests
    SRCS raise_error_test.cpp
    DEPENDS
      libc.hdr.signal_macros
      libc.src.__support.OSUtil.linux.syscall_wrappers.raise
  )

  add_libc_test(
    sigaltstack_registration_test
    SUITE libc_signal_unittests
    SRCS sigaltstack_registration_test.cpp
    DEPENDS
      libc.hdr.signal_macros
      libc.src.signal.sigaltstack
      libc.src.errno.errno
      libc.test.UnitTest.ErrnoCheckingTest
  )

  add_libc_test(
    signal_initialization_test
    SUITE libc_signal_unittests
    SRCS signal_initialization_test.cpp
    COMPILE_OPTIONS
      -ftrivial-auto-var-init=pattern
    DEPENDS
      libc.hdr.signal_macros
      libc.hdr.types.struct_sigaction
      libc.src.__support.common
  )
endif()

add_libc_test(
  raise_test
  SUITE
    libc_signal_unittests
  SRCS
    raise_test.cpp
  DEPENDS
    libc.include.signal
    libc.src.signal.raise
)

add_libc_test(
  kill_test
  SUITE
    libc_signal_unittests
  SRCS
    kill_test.cpp
  DEPENDS
    libc.include.signal
    libc.src.signal.kill
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  sigaction_test
  SUITE
    libc_signal_unittests
  SRCS
    sigaction_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.signal_macros
    libc.src.signal.raise
    libc.src.signal.sigaction
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  sigprocmask_test
  SUITE
    libc_signal_unittests
  SRCS
    sigprocmask_test.cpp
  DEPENDS
    libc.src.errno.errno
    libc.src.signal.raise
    libc.src.signal.sigaddset
    libc.src.signal.sigemptyset
    libc.src.signal.sigprocmask
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  pthread_sigmask_test
  SUITE
    libc_signal_unittests
  SRCS
    pthread_sigmask_test.cpp
  DEPENDS
    libc.src.errno.errno
    libc.src.signal.pthread_sigmask
    libc.src.signal.raise
    libc.src.signal.sigaddset
    libc.src.signal.sigemptyset
    libc.test.UnitTest.ErrnoCheckingTest
)

add_libc_test(
  sigaddset_test
  SUITE
    libc_signal_unittests
  SRCS
    sigaddset_test.cpp
  DEPENDS
    libc.include.signal
    libc.src.errno.errno
    libc.src.signal.sigaddset
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  signal_test
  SUITE
    libc_signal_unittests
  SRCS
    signal_test.cpp
  DEPENDS
    libc.src.errno.errno
    libc.src.signal.raise
    libc.src.signal.signal
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  sigfillset_test
  SUITE
    libc_signal_unittests
  SRCS
    sigfillset_test.cpp
  DEPENDS
    libc.include.signal
    libc.src.errno.errno
    libc.src.signal.raise
    libc.src.signal.sigfillset
    libc.src.signal.sigprocmask
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  sigdelset_test
  SUITE
    libc_signal_unittests
  SRCS
    sigdelset_test.cpp
  DEPENDS
    libc.include.signal
    libc.src.errno.errno
    libc.src.signal.raise
    libc.src.signal.sigdelset
    libc.src.signal.sigfillset
    libc.src.signal.sigprocmask
    libc.test.UnitTest.ErrnoSetterMatcher
)

add_libc_test(
  sigaltstack_test
  SUITE
    libc_signal_unittests
  SRCS
    sigaltstack_test.cpp
  DEPENDS
    libc.hdr.signal_macros
    libc.hdr.stdint_proxy
    libc.src.errno.errno
    libc.src.signal.raise
    libc.src.signal.sigaltstack
    libc.src.signal.sigaction
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)
