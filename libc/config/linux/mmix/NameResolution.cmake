option(LIBC_MMIX_BUILD_NAME_RESOLUTION
  "Build Linux numeric address and DNS packet interfaces" OFF)
if(NOT LIBC_MMIX_BUILD_NAME_RESOLUTION)
  return()
endif()
# FIXME: Name/service lookup and IPv6 resolution need separate providers and
# qualification; packet construction/parsing does not provide a DNS transport.
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.arpa.inet.inet_addr
  libc.src.arpa.inet.inet_aton
  libc.src.arpa.inet.inet_ntoa
  libc.src.arpa.inet.inet_ntop
  libc.src.arpa.inet.inet_pton
  libc.src.arpa.inet.htons
  libc.src.arpa.inet.htonl
  libc.src.arpa.inet.ntohs
  libc.src.arpa.inet.ntohl
  libc.src.netdb.getaddrinfo
  libc.src.netdb.freeaddrinfo
  libc.src.netdb.getnameinfo
  libc.src.resolv.dn_expand
  libc.src.resolv.dn_skipname
  libc.src.resolv.ns_get16
  libc.src.resolv.ns_get32
  libc.src.resolv.ns_initparse
  libc.src.resolv.ns_name_uncompress
  libc.src.resolv.ns_parserr
  libc.src.resolv.ns_skiprr
  libc.src.resolv.res_mkquery)
