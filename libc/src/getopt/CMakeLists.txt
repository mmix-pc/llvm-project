add_entrypoint_object(
  getopt_long
  SRCS getopt_long.cpp
  HDRS getopt_long.h
  DEPENDS
    libc.hdr.types.struct_option
    libc.src.unistd.getopt
    libc.src.stdlib.getenv
    libc.src.__support.common
)

add_entrypoint_object(
  getopt_long_only
  SRCS getopt_long_only.cpp
  HDRS getopt_long_only.h
  DEPENDS
    libc.hdr.types.struct_option
    libc.src.unistd.getopt
    libc.src.stdlib.getenv
    libc.src.__support.common
)
