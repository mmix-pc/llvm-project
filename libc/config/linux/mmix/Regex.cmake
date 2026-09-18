option(LIBC_MMIX_BUILD_REGEX "Build basic and extended regular expressions" OFF)
if(LIBC_MMIX_BUILD_REGEX)
  if(NOT LIBC_MMIX_BUILD_CORE)
    message(FATAL_ERROR "MMIX regex requires core string and character services")
  endif()
  list(APPEND TARGET_LIBC_ENTRYPOINTS
    libc.src.regex.regcomp
    libc.src.regex.regexec
    libc.src.regex.regerror
    libc.src.regex.regfree)
endif()
