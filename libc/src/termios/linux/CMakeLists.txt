# There is no difference between input and output speeds on Linux.
# However, since POSIX requires separate functions for setting and getting
# of the input and output speeds, we use different entrypoints with the
# same getter/setter logic.

foreach(entry tcgetwinsize tcsetwinsize)
  add_entrypoint_object(
    ${entry}
    SRCS ${entry}.cpp
    HDRS ../${entry}.h
    DEPENDS
      libc.hdr.types.struct_winsize
      libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
      libc.src.__support.macros.null_check
      libc.src.errno.errno
  )
endforeach()

add_header_library(
  speed_utils
  HDRS
    speed_utils.h
  DEPENDS
    libc.hdr.termios_macros
    libc.hdr.types.speed_t
)

add_entrypoint_object(
  cfgetispeed
  SRCS
    cfgetispeed.cpp
  HDRS
    ../cfgetispeed.h
  DEPENDS
    libc.hdr.types.speed_t
    libc.hdr.types.struct_termios
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  cfsetispeed
  SRCS
    cfsetispeed.cpp
  HDRS
    ../cfsetispeed.h
  DEPENDS
    .speed_utils
    libc.hdr.types.speed_t
    libc.hdr.types.struct_termios
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  cfgetospeed
  SRCS
    cfgetospeed.cpp
  HDRS
    ../cfgetospeed.h
  DEPENDS
    libc.hdr.types.speed_t
    libc.hdr.types.struct_termios
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  cfsetospeed
  SRCS
    cfsetospeed.cpp
  HDRS
    ../cfsetospeed.h
  DEPENDS
    .speed_utils
    libc.hdr.types.speed_t
    libc.hdr.types.struct_termios
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  tcgetsid
  SRCS
    tcgetsid.cpp
  HDRS
    ../tcgetsid.h
  DEPENDS
    libc.hdr.types.pid_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)

set(tcdrain_source tcdrain.cpp)
set(tcdrain_deps)
set(tcdrain_options)
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  set(tcdrain_source mmix/tcdrain.cpp)
  list(APPEND tcdrain_deps libc.include.sys_syscall)
  if(LIBC_MMIX_BUILD_CANCELLATION_WINDOWS)
    list(APPEND tcdrain_deps libc.src.__support.threads.linux.mmix.cancellation_point)
    set(tcdrain_options -fexceptions -funwind-tables)
  endif()
endif()
add_entrypoint_object(
  tcdrain
  SRCS
    ${tcdrain_source}
  HDRS
    ../tcdrain.h
  DEPENDS
    ${tcdrain_deps}
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
  COMPILE_OPTIONS ${tcdrain_options}
)

add_entrypoint_object(
  tcflush
  SRCS
    tcflush.cpp
  HDRS
    ../tcflush.h
  DEPENDS
    libc.hdr.termios_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)

add_entrypoint_object(
  tcflow
  SRCS
    tcflow.cpp
  HDRS
    ../tcflow.h
  DEPENDS
    libc.hdr.termios_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)

add_entrypoint_object(
  tcsendbreak
  SRCS
    tcsendbreak.cpp
  HDRS
    ../tcsendbreak.h
  DEPENDS
    libc.hdr.limits_macros
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)

add_header_library(
  kernel_termios
  HDRS
    kernel_termios.h
  DEPENDS
    libc.hdr.types.cc_t
    libc.hdr.types.tcflag_t
)

add_entrypoint_object(
  tcgetattr
  SRCS
    tcgetattr.cpp
  HDRS
    ../tcgetattr.h
  DEPENDS
    .kernel_termios
    .speed_utils
    libc.hdr.termios_macros
    libc.hdr.types.struct_termios
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)

add_entrypoint_object(
  tcsetattr
  SRCS
    tcsetattr.cpp
  HDRS
    ../tcsetattr.h
  DEPENDS
    .kernel_termios
    .speed_utils
    libc.hdr.termios_macros
    libc.hdr.types.struct_termios
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
    libc.src.__support.macros.null_check
)
