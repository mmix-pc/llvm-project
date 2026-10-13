# Bootstrap objects must not depend on initialized libc or compiler runtimes.
foreach(flags CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
    CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL
    LIBC_COMPILE_OPTIONS_DEFAULT)
  if("${${flags}}" MATCHES "(^|[ ;])(-pg|-finstrument-functions[^ ;]*)([ ;]|$)")
    message(FATAL_ERROR "Loader bootstrap does not support function instrumentation")
  endif()
endforeach()
set(LIBC_LOADER_COMPILE_OPTIONS
  -ffreestanding -fno-builtin -fPIC -fvisibility=hidden
  -fno-exceptions -fno-rtti -fno-threadsafe-statics
  -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables
  -fno-lto -fno-sanitize=all -fno-profile-instr-generate
  -fno-profile-generate -fno-profile-arcs -fno-test-coverage
  -fno-coverage-mapping -Werror=global-constructors -Werror=exit-time-destructors
)
