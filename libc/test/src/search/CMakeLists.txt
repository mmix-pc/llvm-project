add_custom_target(libc_search_unittests)
if(LIBC_TARGET_OS_IS_LINUX AND NOT LLVM_LIBC_FULL_BUILD)
  add_libc_test(
    hsearch_allocation_test
    OVERLAY_BUILD_ONLY
    SUITE libc_search_unittests
    SRCS
      hsearch_allocation_test.cpp
      ${LIBC_SOURCE_DIR}/src/search/hcreate.cpp
      ${LIBC_SOURCE_DIR}/src/search/hsearch.cpp
      ${LIBC_SOURCE_DIR}/src/search/hdestroy.cpp
      ${LIBC_SOURCE_DIR}/src/search/hsearch/global.cpp
    DEPENDS
      libc.src.__support.HashTable.table
      libc.src.__support.HashTable.randomness
      libc.src.errno.errno
    # The linker wrappers must observe changes to the injection state.
    COMPILE_OPTIONS -fno-builtin-aligned_alloc -fno-builtin-free
    LINK_LIBRARIES "-Wl,--wrap=aligned_alloc,--wrap=free"
  )

  add_libc_test(
    tsearch_allocation_test
    OVERLAY_BUILD_ONLY
    SUITE libc_search_unittests
    SRCS
      tsearch_allocation_test.cpp
      ${LIBC_SOURCE_DIR}/src/search/tsearch.cpp
      ${LIBC_SOURCE_DIR}/src/search/tfind.cpp
      ${LIBC_SOURCE_DIR}/src/search/tdelete.cpp
    DEPENDS
      libc.hdr.types.posix_tnode
      libc.src.__support.weak_avl
    LINK_LIBRARIES "-Wl,--wrap=malloc,--wrap=free"
  )
endif()

add_libc_test(
  hsearch_test
  SUITE
    libc_search_unittests
  SRCS
    hsearch_test.cpp
  DEPENDS
    libc.src.search.hsearch_r
    libc.src.search.hcreate_r
    libc.src.search.hdestroy_r
    libc.src.search.hsearch
    libc.src.search.hcreate
    libc.src.search.hdestroy
    libc.src.errno.errno
)

add_libc_test(
  insque_test
  SUITE
    libc_search_unittests
  SRCS
    insque_test.cpp
  DEPENDS
    libc.src.search.insque
    libc.src.search.remque
)

add_libc_test(
  lfind_test
  SUITE
    libc_search_unittests
  SRCS
    lfind_test.cpp
  DEPENDS
    libc.src.search.lfind
)

add_libc_test(
  lsearch_test
  SUITE
    libc_search_unittests
  SRCS
    lsearch_test.cpp
  DEPENDS
    libc.src.search.lsearch
)

add_libc_test(
  tsearch_test
  SUITE
    libc_search_unittests
  SRCS
    tsearch_test.cpp
  DEPENDS
    libc.src.search.tsearch
    libc.src.search.tfind
    libc.src.search.tdelete
    libc.src.search.twalk
    libc.src.search.twalk_r
    libc.src.search.tdestroy
    libc.src.string.strlen
    libc.src.string.strcpy
    libc.src.string.strcmp
)
