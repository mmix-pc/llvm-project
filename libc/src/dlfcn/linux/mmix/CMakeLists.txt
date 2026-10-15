add_object_library(
  loading
  SRCS loading.cpp
  HDRS loading.h
  DEPENDS libc.src.__support.OSUtil.linux.loader_services
    libc.src.__support.threads.linux.mmix.loader_cancellation
    libc.hdr.errno_macros
  COMPILE_OPTIONS -fno-lto -ftls-model=initial-exec
)
foreach(name dlopen dlsym dlerror dlclose dladdr dlinfo)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ../../${name}.h
    DEPENDS .loading libc.src.__support.common libc.include.dlfcn libc.hdr.types.dl_info
    COMPILE_OPTIONS -fno-lto
  )
endforeach()

add_entrypoint_object(
  dl_iterate_phdr
  SRCS dl_iterate_phdr.cpp
  HDRS ../../../link/dl_iterate_phdr.h
  DEPENDS .loading libc.src.__support.common libc.hdr.types.struct_dl_phdr_info
    libc.include.link libc.include.elf
  COMPILE_OPTIONS -fno-lto
)
