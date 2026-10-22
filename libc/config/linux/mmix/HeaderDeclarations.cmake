if(LIBC_MMIX_BUILD_SHARED_LIBC)
  # Public declarations and independently buildable objects. Installed export
  # selection waits for the complete runtime loading composition.
  list(APPEND TARGET_ENTRYPOINT_NAME_LIST dlopen dlsym dlerror dlclose dladdr dlinfo dl_iterate_phdr)
endif()

if(NOT LIBC_MMIX_ENABLE_UNIX_HEADERS)
  return()
endif()

# Header availability and namespace selection are independent of runtime
# admission. This directory-local setting does not select archive providers or
# advertise capability macros; HeaderVisibility.yaml controls public namespaces.
set(LLVM_LIBC_ALL_HEADERS ON)
