if(LIBC_MMIX_BUILD_SHARED_LIBC)
  set(atexit_source linux/mmix/atexit.cpp)
  list(APPEND atexit_deps .malloc .free libc.src.__support.CPP.new
    libc.src.__support.threads.linux.mmix.loader_cancellation
    libc.src.__support.OSUtil.linux.mmix.dynamic_tls)
endif()
