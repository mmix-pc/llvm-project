set(signal_set_source_dir "")
set(signal_set_deps .signal_utils)
set(signal_action_source sigaction.cpp)
set(signal_action_deps .__restore .signal_utils)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  add_subdirectory(mmix)
  # Keep mask operations independent of unprovided handler/vDSO machinery.
  set(signal_set_source_dir "mmix/")
  set(signal_set_deps libc.hdr.signal_macros)
  set(signal_action_source mmix/sigaction.cpp)
  set(signal_action_deps .mmix.rt_sigaction .mmix.signal_trampoline)
endif()

add_header_library(
  signal_utils
  HDRS
    signal_utils.h
  DEPENDS
    libc.hdr.types.siginfo_t
    libc.hdr.types.sigset_t
    libc.hdr.types.size_t
    libc.hdr.types.struct_sigaction
    libc.include.sys_syscall
    libc.src.__support.OSUtil.linux.vdso
    libc.src.__support.OSUtil.osutil
    libc.src.__support.OSUtil.linux.syscall_wrappers.rt_sigprocmask
    libc.src.__support.error_or
    libc.src.__support.threads.raw_rwlock
)

add_entrypoint_object(
  kill
  SRCS
    kill.cpp
  HDRS
    ../kill.h
  DEPENDS
    libc.hdr.types.pid_t
    libc.src.errno.errno
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
)

add_entrypoint_object(
  raise
  SRCS
    raise.cpp
  HDRS
    ../raise.h
  DEPENDS
    libc.src.__support.OSUtil.linux.syscall_wrappers.raise
    libc.src.errno.errno
)

if(TARGET libc.src.__support.OSUtil.osutil)
  add_object_library(
    __restore
    SRCS
      __restore.cpp
    COMPILE_OPTIONS
      -fomit-frame-pointer
      ${libc_opt_high_flag}
      -Wframe-larger-than=0
      -Wno-attributes
      # asan creates asan.module_ctor which uses stack space, causing warnings.
      -fno-sanitize=address
    DEPENDS
      libc.include.sys_syscall
      libc.src.__support.OSUtil.osutil
  )
endif()

add_entrypoint_object(
  sigaction
  SRCS
    ${signal_action_source}
  HDRS
    ../sigaction.h
  DEPENDS
    ${signal_action_deps}
    libc.hdr.errno_macros
    libc.hdr.signal_macros
    libc.src.errno.errno
)

add_entrypoint_object(
  sigaltstack
  SRCS
    sigaltstack.cpp
  HDRS
    ../sigaltstack.h
  DEPENDS
    libc.include.signal
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
    libc.hdr.types.stack_t
)

add_entrypoint_object(
  sigprocmask
  SRCS
    ${signal_set_source_dir}sigprocmask.cpp
  HDRS
    ../sigprocmask.h
  DEPENDS
    ${signal_set_deps}
    libc.hdr.types.sigset_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.rt_sigprocmask
    libc.src.errno.errno
)

add_entrypoint_object(
  sigismember
  SRCS
    sigismember.cpp
  HDRS
    ../sigismember.h
  DEPENDS
    libc.hdr.signal_macros
    libc.hdr.types.sigset_t
    libc.src.errno.errno
)

add_entrypoint_object(
  sigsuspend
  SRCS
    sigsuspend.cpp
  HDRS
    ../sigsuspend.h
  DEPENDS
    libc.hdr.signal_macros
    libc.hdr.types.sigset_t
    libc.include.sys_syscall
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  pthread_sigmask
  SRCS
    pthread_sigmask.cpp
  HDRS
    ../pthread_sigmask.h
  DEPENDS
    libc.hdr.types.sigset_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.rt_sigprocmask
    libc.src.__support.common
    libc.src.__support.macros.config
)

add_entrypoint_object(
  sigemptyset
  SRCS
    ${signal_set_source_dir}sigemptyset.cpp
  HDRS
    ../sigemptyset.h
  DEPENDS
    ${signal_set_deps}
    libc.hdr.types.sigset_t
    libc.src.errno.errno
)

add_entrypoint_object(
  sigaddset
  SRCS
    ${signal_set_source_dir}sigaddset.cpp
  HDRS
    ../sigaddset.h
  DEPENDS
    ${signal_set_deps}
    libc.hdr.types.sigset_t
    libc.src.errno.errno
)

add_entrypoint_object(
  signal
  SRCS
    signal.cpp
  HDRS
    ../signal.h
  DEPENDS
    .sigaction
    libc.hdr.signal_macros
)

add_entrypoint_object(
  sigfillset
  SRCS
    ${signal_set_source_dir}sigfillset.cpp
  HDRS
    ../sigfillset.h
  DEPENDS
    ${signal_set_deps}
    libc.hdr.types.sigset_t
    libc.src.errno.errno
)

add_entrypoint_object(
  sigdelset
  SRCS
    ${signal_set_source_dir}sigdelset.cpp
  HDRS
    ../sigdelset.h
  DEPENDS
    ${signal_set_deps}
    libc.hdr.types.sigset_t
    libc.src.errno.errno
)
