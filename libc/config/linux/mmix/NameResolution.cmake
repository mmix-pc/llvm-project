option(LIBC_MMIX_BUILD_NAME_RESOLUTION
  "Build Linux name-resolution entrypoints (currently numeric IPv4 only)" OFF)
if(NOT LIBC_MMIX_BUILD_NAME_RESOLUTION)
  return()
endif()
# FIXME: Name/service lookup and IPv6 resolution need separate providers and
# qualification; numeric IPv4 support does not establish DNS availability.
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.arpa.inet.inet_aton
  libc.src.arpa.inet.inet_ntop
  libc.src.arpa.inet.htons
  libc.src.netdb.getaddrinfo
  libc.src.netdb.freeaddrinfo
  libc.src.netdb.getnameinfo)
