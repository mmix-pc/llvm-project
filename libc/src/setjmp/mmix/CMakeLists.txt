if(LIBC_TARGET_OS STREQUAL "linux")
  add_object_library(
    jump_fail
    SRCS linux/jump_fail.S
  )
  foreach(entry setjmp sigsetjmp)
    add_entrypoint_object(
      ${entry}
      SRCS linux/${entry}.cpp
      HDRS ../setjmp_impl.h ../sigsetjmp.h linux/save.h
      DEPENDS
        .jump_fail
        libc.hdr.types.jmp_buf
        libc.hdr.types.sigjmp_buf
        libc.include.sys_syscall
        libc.src.__support.common
        libc.src.__support.macros.macro_utils
    )
  endforeach()
  # Linux restore needs kernel preparation; never use the Generic longjmp.
  return()
endif()

add_entrypoint_object(
  setjmp
  SRCS
    setjmp.cpp
  HDRS
    ../setjmp_impl.h
  DEPENDS
    libc.hdr.types.jmp_buf
  COMPILE_OPTIONS
    ${libc_opt_high_flag}
    -fomit-frame-pointer
)

add_entrypoint_object(
  longjmp
  SRCS
    longjmp.cpp
  HDRS
    ../longjmp.h
  DEPENDS
    libc.hdr.types.jmp_buf
  COMPILE_OPTIONS
    ${libc_opt_high_flag}
    -fomit-frame-pointer
)
