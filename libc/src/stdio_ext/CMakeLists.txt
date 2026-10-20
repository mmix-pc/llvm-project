add_entrypoint_object(
  __fsetlocking
  SRCS __fsetlocking.cpp
  HDRS __fsetlocking.h
  DEPENDS
    libc.hdr.types.FILE
    libc.include.llvm-libc-macros.stdio_ext_macros
    libc.src.__support.File.file
    libc.src.__support.common
)
