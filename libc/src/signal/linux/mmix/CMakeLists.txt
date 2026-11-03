add_header_library(
  signal_names
  HDRS signal_names.h
  DEPENDS libc.hdr.signal_macros libc.src.__support.macros.config
)

foreach(entry sig2str str2sig)
  add_entrypoint_object(
    ${entry}
    SRCS ${entry}.cpp
    HDRS ../../${entry}.h
    DEPENDS .signal_names libc.src.__support.common
  )
endforeach()

add_object_library(
  uapi_checks
  SRCS uapi_checks.cpp
  DEPENDS
    libc.include.signal
    libc.include.setjmp
    libc.include.ucontext
    libc.include.sys_syscall
)

add_header_library(
  private_signals
  HDRS private_signals.h
  DEPENDS libc.src.__support.macros.config libc.src.__support.macros.attributes
)

set(timed_wait_deps)
set(timed_wait_options -fexceptions -funwind-tables)
if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
  list(APPEND timed_wait_deps
    libc.src.__support.threads.linux.mmix.cancellation_point)
  if(LIBC_MMIX_BUILD_SIGNAL_CANCELLATION)
    list(APPEND timed_wait_options -DLIBC_MMIX_SIGNAL_WAIT)
  endif()
endif()
add_header_library(
  timed_wait
  HDRS timed_wait.h
  DEPENDS
    .private_signals
    libc.hdr.errno_macros
    libc.hdr.types.siginfo_t
    libc.hdr.types.sigset_t
    libc.hdr.types.struct_timespec
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    ${timed_wait_deps}
)
add_entrypoint_object(
  sigwait
  SRCS sigwait.cpp
  HDRS ../../sigwait.h
  DEPENDS .timed_wait libc.src.__support.common
  COMPILE_OPTIONS ${timed_wait_options}
)

foreach(entry sigpending sigqueue)
  add_entrypoint_object(
    ${entry}
    SRCS ${entry}.cpp
    HDRS ../../${entry}.h
    DEPENDS
      .private_signals
      libc.hdr.signal_macros
      libc.hdr.types.sigset_t
      libc.hdr.types.siginfo_t
      libc.hdr.types.pid_t
      libc.include.sys_syscall
      libc.src.__support.OSUtil.osutil
      libc.src.__support.common
      libc.src.errno.errno
  )
endforeach()

add_object_library(
  signal_trampoline
  SRCS signal_trampoline.S
  HDRS signal_trampoline.h
  DEPENDS
    libc.include.sys_syscall
    libc.src.setjmp.mmix.jump_fail
)

# Selected by public sigaction only in the matched signal cancellation profile.
# The entry and the jump providers must use the same private checkpoint ABI.
if(LIBC_MMIX_BUILD_UNWIND_ROOTS AND LIBC_MMIX_ENABLE_STATIC_TLS)
  add_object_library(
    signal_entry
    SRCS signal_entry.cpp signal_entry.S
    HDRS signal_entry.h signal_entry_record.h
      ${LIBC_SOURCE_DIR}/src/__support/threads/linux/mmix/signal_boundary.h
    DEPENDS
      .signal_trampoline
      libc.include.signal libc.include.ucontext libc.include.sys_syscall
      libc.src.__support.CPP.atomic
      libc.src.__support.macros.attributes
      libc.src.__support.threads.linux.mmix.lifecycle
      libc.src.__support.OSUtil.linux.mmix.syscall
      libc.src.setjmp.mmix.jump_cleanup
    COMPILE_OPTIONS
      -DLIBC_MMIX_JUMP_CLEANUP -fno-lto -funwind-tables
      -fno-optimize-sibling-calls -D_LIBUNWIND_IS_NATIVE_ONLY
      ${mmix_signal_cancellation_options}
      -I${LIBC_SOURCE_DIR}/../libunwind/include
      -I${LIBC_SOURCE_DIR}/../libunwind/src
  )
endif()

add_header_library(
  kernel_sigaction
  HDRS
    kernel_sigaction.h
  DEPENDS
    libc.hdr.signal_macros
    libc.hdr.types.struct_sigaction
    libc.src.__support.common
    libc.src.__support.macros.config
)

add_header_library(
  rt_sigaction
  HDRS
    rt_sigaction.h
  DEPENDS
    .kernel_sigaction
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.__support.error_or
)
