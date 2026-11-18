add_entrypoint_object(
  freeaddrinfo
  SRCS
    freeaddrinfo.cpp
  HDRS
    freeaddrinfo.h
  DEPENDS
    libc.hdr.types.struct_addrinfo
    libc.hdr.func.free
    libc.src.__support.common
    libc.src.__support.macros.config
)

if(NOT (LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND LIBC_TARGET_OS STREQUAL "linux"))
add_entrypoint_object(
  getaddrinfo
  SRCS
    getaddrinfo.cpp
  HDRS
    getaddrinfo.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.netdb_macros
    libc.hdr.types.struct_addrinfo
    libc.src.__support.common
    libc.src.__support.macros.config
    libc.src.errno.errno
    libc.hdr.func.malloc
    libc.hdr.netinet_in_macros
    libc.hdr.sys_socket_macros
    libc.hdr.types.struct_sockaddr_in
    libc.src.__support.net.address
    libc.src.arpa.inet.inet_ntop
    .freeaddrinfo
)

endif()

if(NOT (LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND LIBC_TARGET_OS STREQUAL "linux"))
add_entrypoint_object(
  getnameinfo
  SRCS getnameinfo.cpp
  HDRS getnameinfo.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.netdb_macros
    libc.hdr.sys_socket_macros
    libc.hdr.types.struct_sockaddr_in
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.arpa.inet.inet_ntop
    libc.src.stdio.snprintf
    libc.src.string.string_utils
)
endif()

if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND LIBC_TARGET_OS STREQUAL "linux")
  add_subdirectory(linux/mmix)
  add_entrypoint_object(getnameinfo ALIAS DEPENDS .linux.mmix.getnameinfo)
  add_entrypoint_object(getaddrinfo ALIAS DEPENDS .linux.mmix.getaddrinfo)
  add_entrypoint_object(gai_strerror ALIAS DEPENDS .linux.mmix.gai_strerror)
  add_entrypoint_object(gethostbyname ALIAS DEPENDS .linux.mmix.gethostbyname)
  add_entrypoint_object(gethostbyaddr ALIAS DEPENDS .linux.mmix.gethostbyaddr)
  add_entrypoint_object(__h_errno_location ALIAS DEPENDS .linux.mmix.__h_errno_location)
  add_entrypoint_object(sethostent ALIAS DEPENDS .linux.mmix.sethostent)
  add_entrypoint_object(endhostent ALIAS DEPENDS .linux.mmix.endhostent)
  add_entrypoint_object(gethostent ALIAS DEPENDS .linux.mmix.gethostent)
  add_entrypoint_object(setnetent ALIAS DEPENDS .linux.mmix.setnetent)
  add_entrypoint_object(endnetent ALIAS DEPENDS .linux.mmix.endnetent)
  add_entrypoint_object(getnetent ALIAS DEPENDS .linux.mmix.getnetent)
  add_entrypoint_object(setprotoent ALIAS DEPENDS .linux.mmix.setprotoent)
  add_entrypoint_object(endprotoent ALIAS DEPENDS .linux.mmix.endprotoent)
  add_entrypoint_object(getprotoent ALIAS DEPENDS .linux.mmix.getprotoent)
  add_entrypoint_object(setservent ALIAS DEPENDS .linux.mmix.setservent)
  add_entrypoint_object(endservent ALIAS DEPENDS .linux.mmix.endservent)
  add_entrypoint_object(getservent ALIAS DEPENDS .linux.mmix.getservent)
  add_entrypoint_object(getnetbyname ALIAS DEPENDS .linux.mmix.getnetbyname)
  add_entrypoint_object(getnetbyaddr ALIAS DEPENDS .linux.mmix.getnetbyaddr)
  add_entrypoint_object(getprotobyname ALIAS DEPENDS .linux.mmix.getprotobyname)
  add_entrypoint_object(getprotobynumber ALIAS DEPENDS .linux.mmix.getprotobynumber)
  add_entrypoint_object(getservbyname ALIAS DEPENDS .linux.mmix.getservbyname)
  add_entrypoint_object(getservbyport ALIAS DEPENDS .linux.mmix.getservbyport)
endif()

add_entrypoint_object(
  hstrerror
  SRCS hstrerror.cpp
  HDRS hstrerror.h
  DEPENDS libc.hdr.netdb_macros libc.src.__support.common
)
