set(cancellable_sockets
  accept accept4 connect recv recvfrom recvmsg recvmmsg send sendto sendmsg sendmmsg)
foreach(name IN LISTS cancellable_sockets)
  set(${name}_source ${name}.cpp)
endforeach()
if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix")
  include(${LIBC_SOURCE_DIR}/config/linux/mmix/CancellationPoints.cmake)
  mmix_select_cancellation_points(${cancellable_sockets})
endif()

add_entrypoint_object(
  socket
  SRCS
    socket.cpp
  HDRS
    ../socket.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.socket
    libc.src.errno.errno
)

add_entrypoint_object(
  bind
  SRCS
    bind.cpp
  HDRS
    ../bind.h
  DEPENDS
    libc.include.sys_socket
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.bind
    libc.src.errno.errno
)

add_entrypoint_object(
  accept
  SRCS
    ${accept_source}
  HDRS
    ../accept.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.macros.sanitizer
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  accept4
  SRCS
    ${accept4_source}
  HDRS
    ../accept4.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.macros.sanitizer
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  connect
  SRCS
    ${connect_source}
  HDRS
    ../connect.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  getpeername
  SRCS
    getpeername.cpp
  HDRS
    ../getpeername.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.getpeername
    libc.src.errno.errno
)

add_entrypoint_object(
  getsockname
  SRCS
    getsockname.cpp
  HDRS
    ../getsockname.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.getsockname
    libc.src.errno.errno
)

add_entrypoint_object(
  getsockopt
  SRCS
    getsockopt.cpp
  HDRS
    ../getsockopt.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.socklen_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.getsockopt
    libc.src.errno.errno
)

add_entrypoint_object(
  listen
  SRCS
    listen.cpp
  HDRS
    ../listen.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  socketpair
  SRCS
    socketpair.cpp
  HDRS
    ../socketpair.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.macros.sanitizer
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.socketpair
    libc.src.errno.errno
)

add_entrypoint_object(
  send
  SRCS
    ${send_source}
  HDRS
    ../send.h
  DEPENDS
    libc.hdr.types.size_t
    libc.hdr.types.ssize_t
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.sendto
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  sendto
  SRCS
    ${sendto_source}
  HDRS
    ../sendto.h
  DEPENDS
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.hdr.types.ssize_t
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.sendto
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  setsockopt
  SRCS
    setsockopt.cpp
  HDRS
    ../setsockopt.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.OSUtil.osutil
    libc.hdr.types.socklen_t
    libc.src.__support.OSUtil.linux.syscall_wrappers.setsockopt
    libc.src.errno.errno
)

add_entrypoint_object(
  sendmmsg
  SRCS
    ${sendmmsg_source}
  HDRS
    ../sendmmsg.h
  DEPENDS
    libc.hdr.types.struct_mmsghdr
    libc.src.__support.macros.sanitizer
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.sendmmsg
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  sendmsg
  SRCS
    ${sendmsg_source}
  HDRS
    ../sendmsg.h
  DEPENDS
    libc.hdr.types.struct_msghdr
    libc.hdr.types.ssize_t
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.sendmsg
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)
add_entrypoint_object(
  recv
  SRCS
    ${recv_source}
  HDRS
    ../recv.h
  DEPENDS
    libc.src.__support.macros.sanitizer
    libc.hdr.types.size_t
    libc.hdr.types.ssize_t
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.recvfrom
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  recvfrom
  SRCS
    ${recvfrom_source}
  HDRS
    ../recvfrom.h
  DEPENDS
    libc.hdr.types.struct_sockaddr
    libc.hdr.types.socklen_t
    libc.src.__support.macros.sanitizer
    libc.hdr.types.ssize_t
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.recvfrom
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  recvmmsg
  SRCS
    ${recvmmsg_source}
  HDRS
    ../recvmmsg.h
  DEPENDS
    libc.hdr.types.struct_mmsghdr
    libc.hdr.types.struct_timespec
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.recvmmsg
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  recvmsg
  SRCS
    ${recvmsg_source}
  HDRS
    ../recvmsg.h
  DEPENDS
    libc.hdr.types.struct_msghdr
    libc.src.__support.macros.sanitizer
    libc.hdr.types.ssize_t
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.recvmsg
    libc.src.errno.errno
    ${mmix_cancellation_dependency}
  COMPILE_OPTIONS ${mmix_cancellation_options}
)

add_entrypoint_object(
  shutdown
  SRCS
    shutdown.cpp
  HDRS
    ../shutdown.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.OSUtil.osutil
    libc.src.errno.errno
)

add_entrypoint_object(
  sockatmark
  SRCS
    sockatmark.cpp
  HDRS
    ../sockatmark.h
  DEPENDS
    libc.include.sys_socket
    libc.src.__support.common
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.errno.errno
)
