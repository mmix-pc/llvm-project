if(NOT LIBC_MMIX_ENABLE_UNIX_HEADERS)
  return()
endif()

# Declaration-only preparation; this list does not select archive providers.
# Keep the normal profile's generated declarations unchanged when disabled.
list(APPEND TARGET_ENTRYPOINT_NAME_LIST
  sched_getaffinity prctl
  major minor makedev ioctl tcgetattr tcsetattr tcgetpgrp tcsetpgrp
  setpgid getpgid getsid ttyname cfgetispeed cfgetospeed cfsetispeed cfsetospeed
  execvp sigsuspend poll ppoll alarm utimensat
  getopt getopt_long optarg optind opterr optopt fnmatch regcomp regexec regerror regfree
  getuid geteuid getgid getegid getpwnam getpwuid getgrnam getgrgid endpwent endgrent
  setmntent getmntent getmntent_r endmntent hasmntopt addmntent
  mount umount umount2 reboot sync uname
  strsignal strverscmp asprintf vasprintf getline getdelim
  socket bind listen sendto getsockname getpeername connect setsockopt getsockopt
  htons ntohs htonl ntohl inet_aton inet_ntoa
  getaddrinfo freeaddrinfo getnameinfo gethostbyname
  hstrerror __h_errno_location syslog)
