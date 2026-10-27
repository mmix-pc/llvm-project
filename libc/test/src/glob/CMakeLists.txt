add_custom_target(libc-glob-tests)
add_libc_test(
  component_test
  SUITE libc-glob-tests
  SRCS component_test.cpp
  DEPENDS libc.src.glob.component libc.hdr.errno_macros
)
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
    libc.src.__support.freelist_heap
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

add_libc_test(
  sort_test
  FULL_BUILD_ONLY
  SUITE libc-glob-tests
  SRCS sort_test.cpp
  DEPENDS libc.src.glob.glob libc.src.errno.errno
)

add_libc_test(
  read_error_test
  FULL_BUILD_ONLY
  SUITE libc-glob-tests
  SRCS read_error_test.cpp
  DEPENDS libc.src.glob.glob libc.src.glob.globfree libc.src.errno.errno
)

# Native forced unwind needs the host pthread and C++ runtimes.
if(NOT LLVM_LIBC_FULL_BUILD AND LIBC_TARGET_OS_IS_LINUX AND NOT CMAKE_CROSSCOMPILING)
  add_libc_test(
    unwind_test
    OVERLAY_BUILD_ONLY
    SUITE libc-glob-tests
    SRCS unwind_test.cpp
    DEPENDS libc.src.glob.glob libc.src.glob.globfree libc.src.errno.errno
    COMPILE_OPTIONS -fexceptions -funwind-tables
    LINK_LIBRARIES pthread
  )
  if(TARGET libc.test.src.glob.unwind_test.__build__)
    target_link_options(libc.test.src.glob.unwind_test.__build__ PRIVATE
      -Wl,--wrap=realloc,--wrap=free)
  endif()
endif()
