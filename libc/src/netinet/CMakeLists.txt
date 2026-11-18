add_entrypoint_object(
  in6addr_any
  SRCS
    in6addr_any.cpp
  HDRS
    in6addr_any.h
  DEPENDS
    libc.hdr.netinet_in_macros
    libc.hdr.types.struct_in6_addr
    libc.src.__support.common
    libc.src.__support.macros.config
)

add_entrypoint_object(
  in6addr_loopback
  SRCS
    in6addr_loopback.cpp
  HDRS
    in6addr_loopback.h
  DEPENDS
    libc.hdr.netinet_in_macros
    libc.hdr.types.struct_in6_addr
    libc.src.__support.common
    libc.src.__support.macros.config
)

add_entrypoint_object(ether_aton_r SRCS ether_aton_r.cpp HDRS ether_aton_r.h DEPENDS libc.hdr.types.struct_ether_addr libc.src.__support.common)

add_entrypoint_object(ether_aton SRCS ether_aton.cpp HDRS ether_aton.h DEPENDS libc.hdr.types.struct_ether_addr libc.src.__support.common .ether_aton_r)

add_entrypoint_object(ether_ntoa_r SRCS ether_ntoa_r.cpp HDRS ether_ntoa_r.h DEPENDS libc.hdr.types.struct_ether_addr libc.src.__support.common)

add_entrypoint_object(ether_ntoa SRCS ether_ntoa.cpp HDRS ether_ntoa.h DEPENDS libc.hdr.types.struct_ether_addr libc.src.__support.common .ether_ntoa_r)

if(LIBC_TARGET_OS_IS_LINUX)
add_entrypoint_object(ether_hostton SRCS ether_hostton.cpp HDRS ether_hostton.h DEPENDS libc.hdr.types.struct_ether_addr libc.src.__support.common .ether_aton_r libc.src.__support.OSUtil.linux.syscall_wrappers.open libc.src.__support.OSUtil.linux.syscall_wrappers.read libc.src.__support.OSUtil.linux.syscall_wrappers.close libc.src.errno.errno libc.hdr.errno_macros libc.hdr.fcntl_macros)
endif()
