option(LIBC_MMIX_BUILD_SOCKETS "Build Linux socket providers" OFF)
if(NOT LIBC_MMIX_BUILD_SOCKETS)
  return()
endif()

list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.sys.socket.socket
  libc.src.sys.socket.socketpair
  libc.src.sys.socket.connect
  libc.src.sys.socket.bind
  libc.src.sys.socket.listen
  libc.src.sys.socket.accept
  libc.src.sys.socket.accept4
  libc.src.sys.socket.recv
  libc.src.sys.socket.recvfrom
  libc.src.sys.socket.recvmsg
  libc.src.sys.socket.recvmmsg
  libc.src.sys.socket.send
  libc.src.sys.socket.sendto
  libc.src.sys.socket.sendmsg
  libc.src.sys.socket.sendmmsg
  libc.src.sys.socket.getpeername
  libc.src.sys.socket.getsockname
  libc.src.sys.socket.getsockopt
  libc.src.sys.socket.setsockopt
  libc.src.sys.socket.shutdown
  libc.src.sys.socket.sockatmark
)
