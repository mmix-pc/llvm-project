if(LIBC_TARGET_OS STREQUAL "linux")
  set(jump_cleanup_dependency)
  set(jump_cleanup_options)
  if(LIBC_MMIX_ENABLE_STATIC_TLS)
    add_object_library(
      jump_cleanup
      SRCS linux/jump_cleanup.cpp
      HDRS linux/jump_cleanup.h linux/jump_cleanup_stack.h
      DEPENDS libc.src.__support.macros.attributes
        libc.src.__support.macros.config
      COMPILE_OPTIONS -fno-lto
    )
    set(jump_cleanup_dependency .jump_cleanup)
    set(jump_cleanup_options -DLIBC_MMIX_JUMP_CLEANUP)
  endif()
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
        ${jump_cleanup_dependency}
      COMPILE_OPTIONS ${jump_cleanup_options}
    )
  endforeach()
  add_object_library(
    jump_restore
    SRCS linux/restore.cpp linux/landing.S
    HDRS linux/restore.h
    DEPENDS
      .jump_fail
      libc.hdr.types.jmp_buf
      libc.hdr.signal_macros
      libc.include.sys_syscall
      libc.src.__support.OSUtil.linux.linux_util
      ${jump_cleanup_dependency}
    COMPILE_OPTIONS ${jump_cleanup_options}
  )
  foreach(entry longjmp siglongjmp)
    add_entrypoint_object(
      ${entry}
      SRCS linux/${entry}.cpp
      HDRS ../${entry}.h
      DEPENDS .jump_restore libc.src.__support.common
    )
  endforeach()
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
