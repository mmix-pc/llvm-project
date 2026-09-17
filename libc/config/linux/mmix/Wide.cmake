option(LIBC_MMIX_BUILD_WIDE "Build Linux wide string and formatting providers" OFF)
if(NOT LIBC_MMIX_BUILD_WIDE)
  return()
endif()
foreach(option FLOAT WIDE WRITE_INT)
  if(LIBC_CONF_PRINTF_DISABLE_${option})
    message(FATAL_ERROR "MMIX wide formatting requires printf ${option} support")
  endif()
endforeach()

# Wide streams, scanning and locale databases are separate capabilities.
set(MMIX_WIDE_ENTRYPOINTS
  libc.src.wchar.swprintf
  libc.src.wchar.btowc
  libc.src.wchar.wctob
  libc.src.wchar.wcscat
  libc.src.wchar.wcschr
  libc.src.wchar.wcscmp
  libc.src.wchar.wcscpy
  libc.src.wchar.wcscspn
  libc.src.wchar.wcslen
  libc.src.wchar.wcsncat
  libc.src.wchar.wcsncmp
  libc.src.wchar.wcsncpy
  libc.src.wchar.wcspbrk
  libc.src.wchar.wcsrchr
  libc.src.wchar.wcsspn
  libc.src.wchar.wcsstr
  libc.src.wchar.wcstok
  libc.src.wchar.wcstof
  libc.src.wchar.wcstod
  libc.src.wchar.wcstold
  libc.src.wchar.wcstol
  libc.src.wchar.wcstoll
  libc.src.wchar.wcstoul
  libc.src.wchar.wcstoull
  libc.src.wchar.wmemchr
  libc.src.wchar.wmemcmp
  libc.src.wchar.wmemcpy
  libc.src.wchar.wmemmove
  libc.src.wchar.wmemset
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_WIDE_ENTRYPOINTS})
