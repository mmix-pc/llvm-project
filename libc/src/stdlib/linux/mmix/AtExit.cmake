if(LIBC_MMIX_BUILD_SHARED_LIBC)
  set(atexit_source linux/mmix/atexit.cpp)
  list(APPEND atexit_deps .malloc .free libc.src.__support.CPP.new
    libc.src.__support.threads.linux.mmix.loader_cancellation
    libc.src.__support.OSUtil.linux.mmix.dynamic_tls)
endif()

set(at_quick_exit_source linux/mmix/quick_exit.cpp)
set(at_quick_exit_deps .exit .malloc .free
  libc.src.__support.CPP.new libc.src.__support.CPP.mutex
  libc.src.__support.threads.mutex libc.src.__support.OSUtil.osutil)
if(LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_PLATFORM")
  list(APPEND at_quick_exit_deps
    libc.src.__support.threads.linux.mmix.main_thread)
endif()
if(LIBC_MMIX_BUILD_DEFERRED_CANCELLATION)
  list(APPEND at_quick_exit_options -DLIBC_MMIX_QUICK_EXIT_CANCELLATION)
  list(APPEND at_quick_exit_deps
    libc.src.__support.threads.linux.mmix.loader_cancellation)
endif()
if(LIBC_MMIX_BUILD_SHARED_LIBC)
  list(APPEND at_quick_exit_deps
    libc.src.__support.OSUtil.linux.mmix.dynamic_tls)
endif()
