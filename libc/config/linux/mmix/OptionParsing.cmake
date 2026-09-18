option(LIBC_MMIX_BUILD_OPTION_PARSING "Build short and long command-line option parsing" OFF)
if(LIBC_MMIX_BUILD_OPTION_PARSING)
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.unistd.getopt
    libc.src.getopt.getopt_long
    libc.src.unistd.optarg
    libc.src.unistd.optind
    libc.src.unistd.opterr
    libc.src.unistd.optopt)
endif()
