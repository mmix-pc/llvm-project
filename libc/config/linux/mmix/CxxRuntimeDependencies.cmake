# Keep the published C subset independent of C++ runtime dependencies.
if(NOT LIBC_MMIX_BUILD_CXX_RUNTIME_DEPENDENCIES)
  return()
endif()
foreach(feature FLOAT WIDE WRITE_INT)
  if(LIBC_CONF_PRINTF_DISABLE_${feature})
    message(FATAL_ERROR "MMIX C++ dependencies require printf ${feature} support")
  endif()
endforeach()
list(APPEND TARGET_LIBC_ENTRYPOINTS
  libc.src.assert.__assert_fail
  libc.src.stdlib.ldiv
  libc.src.stdlib.lldiv
  libc.src.stdio.snprintf
  libc.src.stdio.vsnprintf
  libc.src.stdio.vfprintf
  libc.src.stdio.remove
  libc.src.string.strcmp
  libc.src.ctype.isdigit
  libc.src.ctype.isxdigit
  libc.src.pthread.pthread_getunique_np
  libc.src.pthread.pthread_getthreadid_np
  libc.src.sched.sched_yield
  libc.src.time.nanosleep
)
# Pure conversions and the existing C-locale adapters used by libc++ headers.
foreach(name isalnum isalpha isblank iscntrl isgraph islower isprint ispunct
             isspace isupper tolower toupper)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.ctype.${name})
endforeach()
foreach(name abs labs llabs div strtol strtoll strtoul strtoull strtof strtod
             strtold mbtowc)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.stdlib.${name})
endforeach()
foreach(name memchr strchr strrchr strncmp strcpy strncpy strcoll strxfrm)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.string.${name})
endforeach()
foreach(name newlocale freelocale)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.locale.${name})
endforeach()
foreach(name swprintf btowc wctob wcscoll wcsxfrm wcslen wcscmp wcsncmp wcschr
             wcsrchr wcstol wcstoll wcstoul wcstoull wcstof wcstod wcstold
             wmemchr wmemcmp wmemcpy wmemmove wmemset mbrlen mbrtowc mbsrtowcs
             mbsnrtowcs wcrtomb wcsnrtombs getwc ungetwc fputwc)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.wchar.${name})
endforeach()
foreach(name iswalnum iswalpha iswblank iswcntrl iswctype iswdigit iswgraph
             iswlower iswprint iswpunct iswspace iswupper iswxdigit
             towlower towupper wctype)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.wctype.${name})
endforeach()
foreach(name asprintf sscanf getc putc fileno fseeko ftello rename printf puts)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.stdio.${name})
endforeach()
foreach(name strftime clock_gettime timespec_get)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.time.${name})
endforeach()
list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.string.strerror_r)
foreach(name isatty sysconf close read write chdir ftruncate getcwd link
             readlink symlink truncate unlink rmdir pathconf unlinkat)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.unistd.${name})
endforeach()
foreach(name fchmod fchmodat fstat lstat stat mkdir utimensat)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.sys.stat.${name})
endforeach()
foreach(name opendir fdopendir readdir closedir)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.dirent.${name})
endforeach()
list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.fcntl.open libc.src.fcntl.openat
  libc.src.stdlib.realpath libc.src.sys.statvfs.statvfs libc.src.sys.time.utimes)
list(REMOVE_DUPLICATES TARGET_LIBC_ENTRYPOINTS)
