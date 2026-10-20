# Public Unix providers can be composed with pthread storage. The internal
# lifecycle archive and the SINGLE runtime retain their separate selection.
set(MMIX_UNIX_COMPONENTS
  CORE LOCALE SOCKETS SEMAPHORES DESCRIPTORS IOCTL TERMIOS STDIO WIDE FILESYSTEM
  FILE_METADATA VM TIME PROCESS_BASE SIGNAL_MASKS JOB_CONTROL EXEC_SEARCH
  SIGNAL_SUSPEND POLL PROCESS_TIMING PROCESS_CONTROL OPTION_PARSING REGEX
  FNMATCH ACCOUNT_LOOKUP USER_ACCOUNTING MOUNT_TABLE SYSTEM_ADMINISTRATION TEXT_EXTENSIONS
  COMMAND_UTILITIES LOCAL_LOGGING NAME_RESOLUTION ACCOUNT_MANAGEMENT
  EXTENDED_ATTRIBUTES)
if(LIBC_MMIX_BUILD_THREADED_LIBC)
  foreach(component IN LISTS MMIX_UNIX_COMPONENTS)
    option(LIBC_MMIX_BUILD_${component}
      "Build the MMIX Linux ${component} interface family" ON)
  endforeach()
  option(LIBC_MMIX_ENABLE_UNIX_HEADERS
    "Expose additional Unix/Linux API headers and declarations for MMIX Linux" ON)
endif()
# Account management retains pre-creation admission for credential updates.
# Fork/vfork/spawn use the reviewed pthread providers, not SINGLE aggregates.
