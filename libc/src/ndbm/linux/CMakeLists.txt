add_object_library(
  hash_file
  SRCS hash_file.cpp
  HDRS hash_file.h
  DEPENDS
    libc.src.ndbm.hash_database
    libc.src.ndbm.hash_function
    libc.src.ndbm.hash_page
    libc.src.ndbm.hash_write_batch
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.hdr.types.mode_t
    libc.src.__support.CPP.limits
    libc.src.__support.CPP.new
    libc.src.__support.alloc_checker
    libc.src.__support.OSUtil.linux.stat.stat_via_statx
    libc.src.__support.OSUtil.linux.syscall_wrappers.close
    libc.src.__support.OSUtil.linux.syscall_wrappers.fsync
    libc.src.__support.OSUtil.linux.syscall_wrappers.open
    libc.hdr.types.off_t
    libc.src.__support.macros.sanitizer
)

add_object_library(
  database
  SRCS database.cpp
  HDRS database.h
  DEPENDS .hash_file
    libc.src.ndbm.hash_function
    libc.src.ndbm.hash_growth
    libc.src.ndbm.hash_reader
    libc.src.ndbm.hash_rebuild
    libc.src.ndbm.hash_write_batch
    libc.hdr.errno_macros
    libc.src.__support.CPP.new
    libc.src.__support.alloc_checker
    libc.src.string.memory_utils.inline_memcpy
)

if(LLVM_LIBC_FULL_BUILD)
  foreach(name open close error clearerr store delete fetch firstkey nextkey)
    add_entrypoint_object(
      dbm_${name}
      SRCS dbm_${name}.cpp
      HDRS ../dbm_${name}.h
      DEPENDS .database libc.hdr.types.DBM libc.hdr.types.datum
        libc.hdr.types.mode_t libc.src.errno.errno
        libc.src.__support.alloc_checker libc.src.__support.CPP.new
    )
  endforeach()
endif()
