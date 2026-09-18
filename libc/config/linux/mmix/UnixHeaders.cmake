if(NOT LIBC_MMIX_ENABLE_UNIX_HEADERS)
  return()
endif()

foreach(header paths mntent)
  add_header(mmix_${header}
    HDR linux/mmix/${header}.h
    DEST_HDR ${header}.h
    DEPENDS .llvm_libc_common_h .stdio .unistd)
endforeach()
add_header(mmix_sys_reboot
  HDR linux/mmix/reboot.h
  DEST_HDR sys/reboot.h
  DEPENDS .llvm_libc_common_h)
