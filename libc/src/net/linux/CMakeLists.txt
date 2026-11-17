add_entrypoint_object(
  if_indextoname
  SRCS
    if_indextoname.cpp
  HDRS
    ../if_indextoname.h
  DEPENDS
    libc.hdr.net_if_macros
    libc.hdr.errno_macros
    libc.hdr.sys_ioctl_macros
    libc.hdr.sys_socket_macros
    libc.hdr.types.struct_ifreq
    libc.src.__support.CPP.string_view
    libc.src.__support.common
    libc.src.__support.macros.null_check
    libc.src.__support.OSUtil.linux.syscall_wrappers.socket
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.errno.errno
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.string_utils
)

add_entrypoint_object(
  if_nametoindex
  SRCS
    if_nametoindex.cpp
  HDRS
    ../if_nametoindex.h
  DEPENDS
    libc.hdr.net_if_macros
    libc.hdr.errno_macros
    libc.hdr.sys_ioctl_macros
    libc.hdr.sys_socket_macros
    libc.hdr.types.struct_ifreq
    libc.src.__support.CPP.string_view
    libc.src.__support.common
    libc.src.__support.macros.null_check
    libc.src.__support.OSUtil.linux.syscall_wrappers.socket
    libc.src.__support.OSUtil.linux.syscall_wrappers.ioctl
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.errno.errno
    libc.src.string.memory_utils.inline_memcpy
)

add_header_library(
  if_nameindex_impl
  HDRS
    if_nameindex_impl.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.net_if_macros
    libc.hdr.sys_socket_macros
    libc.hdr.types.socklen_t
    libc.hdr.types.ssize_t
    libc.hdr.types.struct_if_nameindex
    libc.src.__support.CPP.new
    libc.src.__support.CPP.scope
    libc.src.__support.CPP.span
    libc.src.__support.alloc_checker
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.string.memory_utils.inline_memcpy
    libc.src.string.string_utils
)

add_entrypoint_object(
  if_nameindex
  SRCS
    if_nameindex.cpp
  HDRS
    ../if_nameindex.h
  DEPENDS
    .if_nameindex_impl
    libc.hdr.types.struct_if_nameindex
    libc.src.__support.OSUtil.linux.network_syscall_policy
    libc.src.__support.common
    libc.src.__support.error_or
    libc.src.errno.errno
)

add_entrypoint_object(
  if_freenameindex
  SRCS
    if_freenameindex.cpp
  HDRS
    ../if_freenameindex.h
  DEPENDS
    libc.hdr.stdint_proxy
    libc.hdr.types.struct_if_nameindex
    libc.src.__support.CPP.new
    libc.src.__support.common
)

add_header_library(
  ifaddrs_impl
  HDRS ifaddrs_impl.h
  DEPENDS
    libc.hdr.errno_macros libc.hdr.net_if_macros libc.hdr.stdint_proxy
    libc.hdr.sys_socket_macros libc.hdr.types.struct_ifaddrs
    libc.hdr.types.socklen_t libc.hdr.types.ssize_t
    libc.hdr.types.struct_sockaddr_in libc.hdr.types.struct_sockaddr_in6
    libc.src.__support.CPP.new libc.src.__support.error_or
    libc.src.__support.macros.config
)
add_entrypoint_object(
  getifaddrs
  SRCS getifaddrs.cpp
  HDRS ../getifaddrs.h
  DEPENDS .ifaddrs_impl libc.src.__support.OSUtil.linux.network_syscall_policy
    libc.src.errno.errno libc.hdr.func.malloc libc.hdr.func.free
)
add_entrypoint_object(
  freeifaddrs
  SRCS freeifaddrs.cpp
  HDRS ../freeifaddrs.h
  DEPENDS libc.hdr.types.struct_ifaddrs libc.src.errno.errno libc.hdr.func.free
)
