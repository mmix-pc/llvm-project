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
