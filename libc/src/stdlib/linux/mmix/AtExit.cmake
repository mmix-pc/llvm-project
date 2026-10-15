if(LIBC_MMIX_BUILD_SHARED_LIBC)
  set(atexit_source linux/mmix/atexit.cpp)
  list(APPEND atexit_deps .malloc .free libc.src.__support.CPP.new)
endif()
