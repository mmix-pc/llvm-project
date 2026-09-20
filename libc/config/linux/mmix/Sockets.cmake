option(LIBC_MMIX_BUILD_SOCKETS "Build Linux socket connection and listener providers" OFF)
if(NOT LIBC_MMIX_BUILD_SOCKETS)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.sys.socket.socket
  libc.src.sys.socket.connect
  libc.src.sys.socket.bind
  libc.src.sys.socket.listen
  libc.src.sys.socket.accept
)
