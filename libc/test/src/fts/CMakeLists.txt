add_custom_target(libc_fts_unittests)
add_libc_test(
  fts_test
  SUITE libc_fts_unittests
  SRCS fts_test.cpp
  DEPENDS
    libc.src.fts.fts_open
    libc.src.fts.fts_read
    libc.src.fts.fts_children
    libc.src.fts.fts_set
    libc.src.fts.fts_close
    libc.src.fcntl.open
    libc.src.fcntl.fcntl
    libc.src.unistd.close
    libc.src.unistd.getcwd
    libc.src.unistd.chdir
    libc.src.unistd.rmdir
    libc.src.unistd.unlink
    libc.src.unistd.symlink
    libc.src.sys.stat.mkdir
    libc.src.sys.stat.chmod
    libc.src.stdlib.mkdtemp
    libc.src.stdio.snprintf
    libc.src.string.strcpy
    libc.src.string.strcmp
    libc.src.string.strlen
    libc.src.errno.errno
)
if(TARGET libc.test.src.fts.fts_test.__build__)
  target_link_options(libc.test.src.fts.fts_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()
