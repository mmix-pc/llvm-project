add_custom_target(libc-netdb-tests)

add_libc_test(
  allocation_test
  SUITE libc-netdb-tests
  SRCS allocation_test.cpp
  DEPENDS
    libc.hdr.func.malloc
    libc.hdr.func.free
    libc.hdr.errno_macros
    libc.hdr.netdb_macros
    libc.hdr.netinet_in_macros
    libc.hdr.sys_socket_macros
    libc.hdr.types.struct_addrinfo
    libc.hdr.types.struct_sockaddr_in
    libc.src.__support.common
    libc.src.__support.net.address
    libc.src.arpa.inet.inet_ntop
    libc.src.errno.errno
)

add_libc_test(
  netdb_test
  SUITE
    libc-netdb-tests
  SRCS
    netdb_test.cpp
    ${LIBC_SOURCE_DIR}/src/netdb/getnameinfo.cpp
  DEPENDS
    libc.hdr.errno_macros
    libc.hdr.netdb_macros
    libc.hdr.types.struct_addrinfo
    libc.src.errno.errno
    libc.src.netdb.freeaddrinfo
    libc.src.netdb.getaddrinfo
    libc.src.stdio.snprintf
    libc.src.arpa.inet.inet_ntop
    libc.src.string.string_utils
    libc.hdr.netinet_in_macros
    libc.hdr.sys_socket_macros
    libc.hdr.types.struct_sockaddr_in
    libc.src.__support.common
    libc.test.UnitTest.ErrnoCheckingTest
    libc.test.UnitTest.ErrnoSetterMatcher
)
