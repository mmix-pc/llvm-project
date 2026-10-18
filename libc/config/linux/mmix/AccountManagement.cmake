option(LIBC_MMIX_BUILD_ACCOUNT_MANAGEMENT
  "Build Linux credential setters and supplementary group management" OFF)
if(NOT LIBC_MMIX_BUILD_ACCOUNT_MANAGEMENT)
  return()
endif()
# Credential-changing providers serialize with creation and reject updates
# after another thread has been created; they do not broadcast credentials.
if(NOT LIBC_CONF_THREAD_MODE STREQUAL "LIBC_THREAD_MODE_SINGLE" AND
   NOT LIBC_MMIX_BUILD_THREADED_LIBC)
  message(FATAL_ERROR "MMIX account management requires single-thread configuration")
endif()
if(NOT LIBC_MMIX_BUILD_ACCOUNT_LOOKUP)
  message(FATAL_ERROR "MMIX account management requires file-backed account queries")
endif()
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.unistd.setuid
  libc.src.unistd.setgid
  libc.src.unistd.seteuid
  libc.src.unistd.setegid
  libc.src.unistd.getgroups
  libc.src.grp.initgroups
  libc.src.grp.endgrent)
