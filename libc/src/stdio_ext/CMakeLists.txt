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

foreach(entry __freadable __fwritable __freading __fwriting __flbf __fbufsize
              __fpending __fpurge)
  add_entrypoint_object(
    ${entry}
    SRCS ${entry}.cpp
    HDRS ${entry}.h
    DEPENDS
      libc.hdr.types.FILE
      libc.src.__support.File.file
      libc.src.__support.common
  )
endforeach()

add_entrypoint_object(
  _flushlbf
  SRCS _flushlbf.cpp
  HDRS _flushlbf.h
  DEPENDS
    libc.src.__support.CPP.scope
    libc.src.__support.File.file
    libc.src.__support.common
    libc.src.__support.libc_errno
    libc.src.stdio.stdin
    libc.src.stdio.stdout
    libc.src.stdio.stderr
)
