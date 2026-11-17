add_custom_target(libc_signal_unittests)

add_libc_test(
  wait_dispatch_test
  SUITE libc_signal_unittests
  SRCS wait_dispatch_test.cpp
    ${LIBC_SOURCE_DIR}/src/signal/sigwait.cpp
    ${LIBC_SOURCE_DIR}/src/signal/sigwaitinfo.cpp
  DEPENDS
    libc.hdr.types.sigset_t
    libc.hdr.types.siginfo_t
    libc.hdr.types.struct_timespec
    libc.src.errno.errno
)

add_libc_test(
  killpg_dispatch_test
  SUITE libc_signal_unittests
  SRCS killpg_dispatch_test.cpp
    ${LIBC_SOURCE_DIR}/src/signal/killpg.cpp
  DEPENDS
    libc.hdr.limits_macros
    libc.hdr.types.pid_t
    libc.src.errno.errno
)

if(LIBC_TARGET_OS STREQUAL "linux" AND
   LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  add_libc_test(
    mmix_signal_names_test
    SUITE libc_signal_unittests
    SRCS mmix_signal_names_test.cpp
    DEPENDS
      libc.hdr.signal_macros
      libc.src.signal.sig2str
      libc.src.signal.str2sig
  )
endif()

add_libc_test(
  psiginfo_test
  FULL_BUILD_ONLY
  SUITE libc_signal_unittests
  SRCS psiginfo_test.cpp ${LIBC_SOURCE_DIR}/src/signal/psiginfo.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.limits_macros
    libc.hdr.signal_macros
    libc.hdr.stdint_proxy
    libc.hdr.stdio_macros
    libc.hdr.types.siginfo_t
    libc.src.__support.CPP.stringstream
    libc.src.__support.CPP.string_view
    libc.src.__support.File.file
    libc.src.__support.File.platform_file
    libc.src.__support.StringUtil.diagnostic_message
    libc.src.__support.StringUtil.platform_signals
    libc.src.__support.StringUtil.signal_to_string
    libc.src.__support.integer_to_string
    libc.src.errno.errno
    libc.src.stdio.stderr
    libc.src.string.strsignal
    libc.test.src.locale_guard
)

add_libc_test(
  psignal_test
  FULL_BUILD_ONLY
  SUITE libc_signal_unittests
  SRCS psignal_test.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.signal_macros
    libc.hdr.stdio_macros
    libc.src.__support.File.file
    libc.src.errno.errno
    libc.src.signal.psignal
    libc.src.stdio.stderr
    libc.src.string.strsignal
    libc.test.src.locale_guard
)

if(LIBC_TARGET_OS STREQUAL "linux")
  if(LLVM_LIBC_FULL_BUILD)
    add_libc_test(
      sigtimedwait_test
      SUITE libc_signal_unittests
      SRCS
        sigtimedwait_test.cpp
        ${LIBC_SOURCE_DIR}/src/signal/linux/sigtimedwait.cpp
      DEPENDS
        libc.hdr.limits_macros
        libc.hdr.signal_macros
        libc.hdr.types.siginfo_t
        libc.hdr.types.sigset_t
        libc.hdr.types.struct_timespec
        libc.include.sys_syscall
        libc.src.__support.OSUtil.osutil
        libc.src.errno.errno
        libc.src.signal.raise
        libc.src.signal.sigaddset
        libc.src.signal.sigemptyset
        libc.src.signal.sigprocmask
    )

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

if(LIBC_TARGET_OS_IS_LINUX)
  add_libc_test(
    sigisemptyset_test
    SUITE libc_signal_unittests
    SRCS sigisemptyset_test.cpp
      ${LIBC_SOURCE_DIR}/src/signal/linux/sigisemptyset.cpp
    DEPENDS libc.hdr.types.sigset_t libc.hdr.signal_macros libc.src.errno.errno
  )
endif()
