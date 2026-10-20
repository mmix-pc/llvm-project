# This engine implements the native LP64 ABI, not a host overlay's compat32 ABI.
if(NOT LIBC_TARGET_OS_IS_LINUX OR NOT LLVM_LIBC_FULL_BUILD)
  return()
endif()
if(NOT (LIBC_TARGET_ARCHITECTURE_IS_MMIX OR
        LIBC_TARGET_ARCHITECTURE_IS_AARCH64 OR
        LIBC_TARGET_ARCHITECTURE_IS_X86_64 OR
        LIBC_TARGET_ARCHITECTURE_IS_RISCV64))
  return()
endif()

set(utmp_cancellation_source cancellation_scope.cpp)
set(utmp_cancellation_deps)
set(utmp_cancellation_options)


add_object_library(
  cancellation_scope
  SRCS ${utmp_cancellation_source}
  HDRS cancellation_scope.h
  DEPENDS
    libc.src.__support.macros.config
    ${utmp_cancellation_deps}
  COMPILE_OPTIONS ${utmp_cancellation_options}
)

add_object_library(
  utmp_engine
  SRCS utmp_engine.cpp
  HDRS utmp_engine.h
  DEPENDS
    .cancellation_scope
    libc.hdr.errno_macros
    libc.hdr.stdio_macros
    libc.hdr.types.struct_utmp
    libc.hdr.types.struct_utmpx
    libc.hdr.utmp_macros
    libc.src.__support.CPP.limits
    libc.src.__support.CPP.mutex
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fcntl
    libc.src.__support.OSUtil.linux.syscall_wrappers.ftruncate
    libc.src.__support.OSUtil.linux.syscall_wrappers.lseek
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.src.__support.OSUtil.linux.syscall_wrappers.read
    libc.src.__support.OSUtil.linux.syscall_wrappers.write
    libc.src.__support.libc_errno
    libc.src.__support.macros.sanitizer
    libc.src.__support.threads.raw_mutex
    libc.src.string.memory_utils.inline_memcpy
  COMPILE_OPTIONS ${utmp_cancellation_options}
)

foreach(name setutent getutent endutent getutid getutline pututline utmpname updwtmp)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS
      .utmp_engine
      libc.hdr.types.struct_utmp
      libc.src.__support.common
    COMPILE_OPTIONS -funwind-tables
  )
endforeach()
