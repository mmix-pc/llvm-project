if(NOT TARGET libc.src.utmp.utmp_engine)
  return()
endif()

foreach(name setutxent getutxent endutxent getutxid getutxline pututxline utmpxname updwtmpx)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ${name}.h
    DEPENDS
      libc.src.utmp.utmp_engine
      libc.hdr.types.struct_utmpx
      libc.src.__support.common
    COMPILE_OPTIONS -funwind-tables
  )
endforeach()
