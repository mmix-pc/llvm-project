if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
endif()

add_entrypoint_object(
  select
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.select
)

if(LIBC_TARGET_ARCHITECTURE STREQUAL "mmix" AND LIBC_TARGET_OS_IS_LINUX)
  add_entrypoint_object(pselect ALIAS DEPENDS .linux.pselect)
endif()
