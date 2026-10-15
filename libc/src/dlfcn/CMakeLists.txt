add_entrypoint_object(
  dlclose
  SRCS
    dlclose.cpp
  HDRS
    dlclose.h
)

if(LIBC_TARGET_OS STREQUAL "linux" AND
   LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND LIBC_MMIX_BUILD_SHARED_LIBC)
  # Build the shared-profile entry objects without selecting an installed API.
  list(APPEND TARGET_ENTRYPOINT_NAME_LIST dlopen dlsym dlerror)
  add_subdirectory(linux/mmix)
  foreach(name dlopen dlsym dlerror)
    add_entrypoint_object(${name} ALIAS DEPENDS .linux.mmix.${name})
  endforeach()
else()
  add_entrypoint_object(
    dlerror
    SRCS
      dlerror.cpp
    HDRS
      dlerror.h
    DEPENDS
      libc.include.dlfcn
  )

  add_entrypoint_object(
    dlopen
    SRCS
      dlopen.cpp
    HDRS
      dlopen.h
    DEPENDS
      libc.include.dlfcn
  )

  add_entrypoint_object(
    dlsym
    SRCS
      dlsym.cpp
    HDRS
      dlsym.h
    DEPENDS
      libc.include.dlfcn
  )
endif()

add_entrypoint_object(
  dlinfo
  SRCS
    dlinfo.cpp
  HDRS
    dlinfo.h
  DEPENDS
    libc.include.dlfcn
)

add_entrypoint_object(
  dladdr
  SRCS
    dladdr.cpp
  HDRS
    dladdr.h
  DEPENDS
    libc.include.dlfcn
    libc.hdr.types.dl_info
)
