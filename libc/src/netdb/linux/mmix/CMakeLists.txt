add_header_library(
  databases
  HDRS databases.h
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.func.free
    libc.hdr.func.realloc
    libc.hdr.sys_socket_macros
    libc.hdr.types.struct_hostent
    libc.hdr.types.struct_netent
    libc.hdr.types.struct_protoent
    libc.hdr.types.struct_servent
    libc.src.__support.OSUtil.osutil
    libc.src.__support.endian_internal
    libc.src.__support.libc_errno
    libc.src.arpa.inet.inet_pton
)

add_entrypoint_object(
  sethostent
  SRCS sethostent.cpp
  HDRS ../../sethostent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  endhostent
  SRCS endhostent.cpp
  HDRS ../../endhostent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  gethostent
  SRCS gethostent.cpp
  HDRS ../../gethostent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  setnetent
  SRCS setnetent.cpp
  HDRS ../../setnetent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  endnetent
  SRCS endnetent.cpp
  HDRS ../../endnetent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getnetent
  SRCS getnetent.cpp
  HDRS ../../getnetent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  setprotoent
  SRCS setprotoent.cpp
  HDRS ../../setprotoent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  endprotoent
  SRCS endprotoent.cpp
  HDRS ../../endprotoent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getprotoent
  SRCS getprotoent.cpp
  HDRS ../../getprotoent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  setservent
  SRCS setservent.cpp
  HDRS ../../setservent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  endservent
  SRCS endservent.cpp
  HDRS ../../endservent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getservent
  SRCS getservent.cpp
  HDRS ../../getservent.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getnetbyname
  SRCS getnetbyname.cpp
  HDRS ../../getnetbyname.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getnetbyaddr
  SRCS getnetbyaddr.cpp
  HDRS ../../getnetbyaddr.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getprotobyname
  SRCS getprotobyname.cpp
  HDRS ../../getprotobyname.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getprotobynumber
  SRCS getprotobynumber.cpp
  HDRS ../../getprotobynumber.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getservbyname
  SRCS getservbyname.cpp
  HDRS ../../getservbyname.h
  DEPENDS .databases libc.src.__support.common
)

add_entrypoint_object(
  getservbyport
  SRCS getservbyport.cpp
  HDRS ../../getservbyport.h
  DEPENDS .databases libc.src.__support.common
)
