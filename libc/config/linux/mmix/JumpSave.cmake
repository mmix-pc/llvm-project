option(LIBC_MMIX_BUILD_JUMP_SAVE "Build Linux setjmp save-side preparation" OFF)
if(NOT LIBC_MMIX_BUILD_JUMP_SAVE)
  return()
endif()

# Save-side preparation includes terminal failure, but not restore providers.
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.setjmp.setjmp
  libc.src.setjmp.sigsetjmp
)
