option(LIBC_MMIX_BUILD_LOCALE "Build built-in C and C.UTF-8 locale providers" OFF)
if(NOT LIBC_MMIX_BUILD_LOCALE)
  return()
endif()

# Built-in locales do not provide an external regional locale database.
set(MMIX_LOCALE_ENTRYPOINTS
  libc.src.locale.__ctype_get_mb_cur_max
  libc.src.langinfo.nl_langinfo
  libc.src.langinfo.nl_langinfo_l
  libc.src.locale.newlocale
  libc.src.locale.freelocale
  libc.src.locale.localeconv
  libc.src.locale.setlocale
  libc.src.string.strcoll
  libc.src.string.strcoll_l
  libc.src.string.strxfrm
  libc.src.string.strxfrm_l
  libc.src.string.strerror_l
  libc.src.stdlib.mbtowc
  libc.src.stdlib.mblen
  libc.src.stdlib.mbstowcs
  libc.src.stdlib.wcstombs
  libc.src.stdlib.wctomb
  libc.src.wchar.mbsinit
  libc.src.wchar.wcsrtombs
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
  libc.src.wctype.wctrans
  libc.src.wctype.towctrans
)
list(APPEND TARGET_LIBC_ENTRYPOINTS ${MMIX_LOCALE_ENTRYPOINTS})
