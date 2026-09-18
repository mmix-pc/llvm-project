if(NOT LIBC_MMIX_ENABLE_UNIX_HEADERS)
  return()
endif()

# Declaration-only preparation; this list does not select archive providers.
# Keep the normal profile's generated declarations unchanged when disabled.
list(APPEND TARGET_ENTRYPOINT_NAME_LIST
  major minor makedev ioctl tcgetattr tcsetattr tcgetpgrp tcsetpgrp
  setpgid getpgid getsid ttyname cfgetispeed cfgetospeed cfsetispeed cfsetospeed
  execvp sigsuspend poll ppoll alarm utimensat
  getopt getopt_long optarg optind opterr optopt fnmatch regcomp regexec regerror regfree
  getuid geteuid getgid getegid getpwnam getpwuid getgrnam getgrgid endpwent endgrent
  mount umount umount2 sync uname
  strsignal strverscmp asprintf vasprintf getline getdelim)
