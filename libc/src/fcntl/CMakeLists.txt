if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${LIBC_TARGET_OS})
endif()

foreach(name IN ITEMS posix_fallocate posix_fadvise)
  if(TARGET libc.src.fcntl.${LIBC_TARGET_OS}.${name})
    add_entrypoint_object(${name} ALIAS DEPENDS .${LIBC_TARGET_OS}.${name})
  endif()
endforeach()

add_entrypoint_object(
  creat
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.creat
)

add_entrypoint_object(
  fcntl
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.fcntl
)

add_entrypoint_object(
  open
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.open
)

add_entrypoint_object(
  openat
  ALIAS
  DEPENDS
    .${LIBC_TARGET_OS}.openat
)
