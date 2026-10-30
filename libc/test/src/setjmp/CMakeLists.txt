add_custom_target(libc_setjmp_unittests)

add_libc_test(
  mmix_jump_cleanup_stack_test
  SUITE libc_setjmp_unittests
  SRCS mmix_jump_cleanup_stack_test.cpp
  DEPENDS libc.src.__support.threads.aio_suspend_observers
    libc.src.__support.macros.attributes libc.src.__support.macros.config
)

# Skip setjmp tests under sanitizers
if(LLVM_USE_SANITIZER)
  return()
endif()

add_libc_test(
  setjmp_test
  SUITE
    libc_setjmp_unittests
  SRCS
    setjmp_test.cpp
  CXX_STANDARD
    20
  DEPENDS
    libc.src.setjmp.longjmp
    libc.src.setjmp.setjmp
)

add_libc_test(
  sigsetjmp_test
  SUITE
    libc_setjmp_unittests
  SRCS
    sigsetjmp_test.cpp
  CXX_STANDARD
    20
  DEPENDS
    libc.src.setjmp.sigsetjmp
    libc.src.setjmp.siglongjmp
    libc.src.signal.sigprocmask
    libc.src.string.memset
    libc.src.string.memcmp
    libc.hdr.types.sigset_t
)
