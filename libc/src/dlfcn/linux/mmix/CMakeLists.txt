add_object_library(
  loading
  SRCS loading.cpp
  HDRS loading.h
  DEPENDS libc.src.__support.OSUtil.linux.loader_services
    libc.src.__support.threads.linux.mmix.loader_cancellation
    libc.hdr.errno_macros
  COMPILE_OPTIONS -fno-lto -ftls-model=initial-exec
)
foreach(name dlopen dlsym dlerror)
  add_entrypoint_object(
    ${name}
    SRCS ${name}.cpp
    HDRS ../../${name}.h
    DEPENDS .loading libc.src.__support.common libc.include.dlfcn
    COMPILE_OPTIONS -fno-lto
  )
endforeach()
