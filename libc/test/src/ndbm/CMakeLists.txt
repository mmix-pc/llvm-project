add_custom_target(libc-ndbm-tests)

add_libc_test(
  hash_validation_test
  SUITE libc-ndbm-tests
  SRCS hash_validation_test.cpp
  DEPENDS libc.src.ndbm.hash_validation
)

add_libc_test(
  hash_page_test
  SUITE libc-ndbm-tests
  SRCS hash_page_test.cpp
  DEPENDS libc.src.ndbm.hash_page
)

add_libc_test(
  hash_io_test
  SUITE libc-ndbm-tests
  SRCS hash_io_test.cpp
  DEPENDS libc.src.ndbm.hash_io libc.src.ndbm.hash_page
)

add_libc_test(
  hash_reader_test
  SUITE libc-ndbm-tests
  SRCS hash_reader_test.cpp
  DEPENDS libc.src.ndbm.hash_reader
)

add_libc_test(
  hash_growth_test
  SUITE libc-ndbm-tests
  SRCS hash_growth_test.cpp
  DEPENDS libc.src.ndbm.hash_growth
)

if(LIBC_TARGET_OS_IS_LINUX AND NOT LLVM_LIBC_FULL_BUILD)
  add_libc_test(
    hash_database_test
    SUITE libc-ndbm-tests
    SRCS hash_database_test.cpp
    DEPENDS libc.src.ndbm.hash_database
    LINK_LIBRARIES "-Wl,--wrap=malloc,--wrap=free"
  )
endif()
