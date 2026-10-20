option(LIBC_MMIX_BUILD_ACCOUNT_LOOKUP "Build Linux file-backed account queries" OFF)
if(NOT LIBC_MMIX_BUILD_ACCOUNT_LOOKUP)
  return()
endif()

set(MMIX_ACCOUNT_LOOKUP_ENTRYPOINTS
  libc.src.unistd.getuid
  libc.src.unistd.geteuid
  libc.src.unistd.getgid
  libc.src.unistd.getegid
  libc.src.pwd.getpwnam_r
  libc.src.pwd.getpwuid_r
  libc.src.pwd.getpwnam
  libc.src.pwd.getpwuid
  libc.src.grp.getgrnam_r
  libc.src.grp.getgrgid_r
  libc.src.grp.getgrnam
  libc.src.grp.getgrgid
  libc.src.grp.setgrent
  libc.src.grp.getgrent
  libc.src.grp.getgrent_r
  libc.src.grp.endgrent
  libc.src.grp.fgetgrent
  libc.src.grp.fgetgrent_r
  libc.src.grp.putgrent
  libc.src.grp.getgrouplist
  libc.src.shadow.getspnam
  libc.src.shadow.getspnam_r
)
foreach(entry IN LISTS MMIX_ACCOUNT_LOOKUP_ENTRYPOINTS)
  if(NOT entry IN_LIST TARGET_LIBC_ENTRYPOINTS)
    list(APPEND TARGET_LIBC_ENTRYPOINTS ${entry})
  endif()
endforeach()
