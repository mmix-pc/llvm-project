option(LIBC_MMIX_BUILD_LOCALE "Build C-locale providers for Linux C++ streams" OFF)
if(NOT LIBC_MMIX_BUILD_LOCALE)
  return()
endif()

# Reuse LLVM libc's C locale; this does not provide named locale databases.
set(MMIX_LOCALE_ENTRYPOINTS
  libc.src.locale.newlocale
  libc.src.locale.freelocale
  libc.src.locale.localeconv
  libc.src.locale.setlocale
  libc.src.string.strcoll
  libc.src.string.strxfrm
  libc.src.stdlib.mbtowc
  libc.src.wchar.mbrlen
  libc.src.wchar.mbrtowc
  libc.src.wchar.mbsrtowcs
  libc.src.wchar.mbsnrtowcs
  libc.src.wchar.wcrtomb
  libc.src.wchar.wcsnrtombs
  libc.src.wchar.wcscoll
  libc.src.wchar.wcsxfrm
  libc.src.wchar.getwc
  libc.src.wchar.ungetwc
  libc.src.wchar.fputwc
  libc.src.wctype.iswalnum
  libc.src.wctype.iswalpha
  libc.src.wctype.iswblank
  libc.src.wctype.iswcntrl
  libc.src.wctype.iswctype
  libc.src.wctype.iswdigit
  libc.src.wctype.iswgraph
  libc.src.wctype.iswlower
  libc.src.wctype.iswprint
  libc.src.wctype.iswpunct
  libc.src.wctype.iswspace
  libc.src.wctype.iswupper
  libc.src.wctype.iswxdigit
  libc.src.wctype.towlower
  libc.src.wctype.towupper
  libc.src.wctype.wctype
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_LOCALE_ENTRYPOINTS})
