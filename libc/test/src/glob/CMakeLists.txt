add_custom_target(libc-glob-tests)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/testdata/sub)
foreach(name a.txt b.txt .hidden literal[ star* sub/leaf)
  file(TOUCH "${CMAKE_CURRENT_BINARY_DIR}/testdata/${name}")
endforeach()
file(CREATE_LINK sub ${CMAKE_CURRENT_BINARY_DIR}/testdata/link SYMBOLIC)
file(CREATE_LINK absent ${CMAKE_CURRENT_BINARY_DIR}/testdata/dangling SYMBOLIC)
file(CREATE_LINK loop ${CMAKE_CURRENT_BINARY_DIR}/testdata/loop SYMBOLIC)

add_libc_test(
  glob_test
  FULL_BUILD_ONLY
  SUITE libc-glob-tests
  SRCS glob_test.cpp
  DEPENDS
    libc.hdr.glob_macros
    libc.src.glob.glob
    libc.src.glob.globfree
    libc.src.fcntl.fcntl
    libc.src.string.strcmp
    libc.src.errno.errno
)
if(TARGET libc.test.src.glob.glob_test.__build__)
  target_link_options(libc.test.src.glob.glob_test.__build__ PRIVATE
    -Wl,--wrap=malloc,--wrap=realloc,--wrap=free)
endif()
