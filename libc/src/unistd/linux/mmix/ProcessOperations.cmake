# Select target-owned admission wrappers without changing generic Linux policy.
foreach(name setuid setgid seteuid setegid)
  set(${name}_source ${name}.cpp)
endforeach()
set(mmix_credential_dependencies "")
if(LIBC_MMIX_ENABLE_STATIC_TLS AND
   LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM")
  foreach(name setuid setgid seteuid setegid)
    set(${name}_source mmix/${name}.cpp)
  endforeach()
  set(mmix_credential_dependencies
    libc.src.__support.threads.linux.mmix.process_operation_h)
endif()
