option(LIBC_MMIX_BUILD_JUMP_SAVE "Build Linux nonlocal-transfer preparation" OFF)
if(NOT LIBC_MMIX_BUILD_JUMP_SAVE)
  return()
endif()

# Restore providers require the kernel's nonreturning preparation service.
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.setjmp.setjmp
  libc.src.setjmp.sigsetjmp
  libc.src.setjmp.longjmp
  libc.src.setjmp.siglongjmp
)
