if(LIBC_TARGET_OS STREQUAL "linux")
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    add_header(mcontext_base HDR mcontext_t.h DEST_HDR mcontext-base.h)
    add_header(mcontext_t HDR cancellation-mcontext_t.h DEST_HDR mcontext_t.h
      DEPENDS .mcontext_base)
  else()
    add_header(mcontext_t HDR mcontext_t.h)
  endif()
  add_header(
    ucontext_t
    HDR ucontext_t.h
    DEPENDS
      .mcontext_t
      libc.include.llvm-libc-types.sigset_t
      libc.include.llvm-libc-types.stack_t
  )
endif()
