option(LIBC_MMIX_BUILD_TEXT_EXTENSIONS
  "Build Linux text, allocating formatting and extended stdio providers" OFF)
if(NOT LIBC_MMIX_BUILD_TEXT_EXTENSIONS)
  return()
endif()
foreach(name mempcpy strchrnul strsep)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.string.${name})
endforeach()
foreach(name asprintf vasprintf dprintf vdprintf getline getdelim
    getc_unlocked getchar_unlocked fgetc_unlocked
    putc_unlocked putchar_unlocked fputc_unlocked
    fgets_unlocked fputs_unlocked feof_unlocked ferror_unlocked fileno_unlocked)
  list(APPEND TARGET_LIBC_ENTRYPOINTS libc.src.stdio.${name})
endforeach()
