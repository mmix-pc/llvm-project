add_subdirectory(mmix)

add_object_library(
  runtime_reclamation
  SRCS runtime_reclamation.cpp
  DEPENDS .runtime_retirement .runtime_load
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_retirement
  SRCS runtime_retirement.cpp
  HDRS runtime_retirement.h
  DEPENDS .runtime_initialization .runtime_queries .runtime_unwind
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_state
  SRCS runtime_state.cpp
  HDRS runtime_state.h
  DEPENDS .runtime_api .runtime_unwind .thread_tls .graph .mmix.debug
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_api
  SRCS runtime_api.cpp
  HDRS runtime_api.h
  DEPENDS .runtime_load .runtime_initialization .runtime_queries .locks .io
    .runtime_reclamation
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS} -funwind-tables
)

add_object_library(
  runtime_queries
  SRCS runtime_queries.cpp
  HDRS runtime_queries.h
  DEPENDS .runtime_scopes .dynamic .arena .io
    libc.hdr.types.dl_info libc.hdr.types.struct_link_map
    libc.hdr.types.struct_r_debug libc.hdr.types.struct_dl_phdr_info
    libc.hdr.func.free libc.src.__support.common libc.hdr.errno_macros
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_initialization
  SRCS runtime_initialization.cpp
  HDRS runtime_initialization.h
  DEPENDS .initialization_plan .image_registry .entry .locks
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_header_library(
  initialization_plan
  HDRS initialization_plan.h
  DEPENDS .arena .dynamic .mmix.target
    libc.hdr.errno_macros libc.hdr.func.free libc.src.__support.common
)

add_object_library(
  runtime_unwind
  SRCS runtime_unwind.cpp
  HDRS runtime_unwind.h
  DEPENDS .runtime_scopes .unwind_images .arena .io
    libc.hdr.errno_macros libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  thread_tls
  SRCS thread_tls.cpp
  HDRS thread_tls.h
  DEPENDS .runtime_tls .runtime_scopes .tls_allocation .mmix.tls_pointer
    .mmix.syscall .mmix.target libc.hdr.func.free libc.src.__support.common
    libc.hdr.errno_macros libc.hdr.sys_mman_macros libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_tls
  SRCS runtime_tls.cpp
  HDRS runtime_tls.h
  DEPENDS .dtv .tls_layout .mmix.syscall .mmix.target
    libc.hdr.errno_macros libc.hdr.signal_macros libc.hdr.sys_mman_macros
    libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  dtv
  SRCS dtv.cpp
  HDRS dtv.h
  DEPENDS .image_registry .io .mmix.syscall .mmix.target .mmix.tls_pointer
    libc.hdr.errno_macros libc.hdr.signal_macros libc.hdr.sys_mman_macros
    libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_load
  SRCS runtime_load.cpp
  HDRS runtime_load.h
  DEPENDS .runtime_graph .runtime_scopes .runtime_unwind .relocation .protection .mmix.target
    libc.hdr.errno_macros libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_scopes
  SRCS runtime_scopes.cpp
  HDRS runtime_scopes.h snapshot_storage.h unwind_leases.h
  DEPENDS .image_registry .symbols .arena .unwind_images libc.src.__support.CPP.atomic
    libc.hdr.func.free libc.src.__support.common libc.hdr.errno_macros
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime_graph
  SRCS runtime_graph.cpp
  HDRS runtime_graph.h
  DEPENDS .image_registry .search .mapping .arena .mmix.syscall .mmix.target
    libc.hdr.errno_macros libc.include.sys_syscall libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  locks
  SRCS locks.cpp
  HDRS locks.h
  DEPENDS .mmix.syscall libc.src.__support.CPP.atomic
    libc.src.__support.OSUtil.linux.fork_recovery
    libc.src.__support.OSUtil.linux.loader_services
    libc.hdr.errno_macros libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS} -funwind-tables
)

add_object_library(
  image_registry
  SRCS image_registry.cpp
  HDRS image_registry.h module_ownership.h retirement_events.h
  DEPENDS .arena .io .search .tls_layout .dynamic libc.hdr.errno_macros
    libc.hdr.func.free libc.src.__support.common libc.src.__support.CPP.atomic
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  runtime
  SRCS runtime.cpp
  HDRS runtime.h
  DEPENDS .graph .entry .tls_resolver .unwind_images .runtime_state libc.hdr.errno_macros
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  unwind_images
  SRCS unwind_images.cpp
  HDRS unwind_images.h
  DEPENDS .elf .arena .io libc.hdr.errno_macros
    libc.src.__support.OSUtil.linux.loader_services
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  tls_resolver
  SRCS tls_resolver.cpp
  HDRS tls_resolver.h
  DEPENDS .tls_allocation .io .mmix.tls_pointer
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  tls_allocation
  SRCS tls_allocation.cpp
  HDRS tls_allocation.h
  DEPENDS .relocation .mmix.syscall .mmix.target libc.hdr.errno_macros
    libc.hdr.sys_mman_macros libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  tls_layout
  SRCS tls_layout.cpp
  HDRS tls_layout.h
  DEPENDS .elf .mmix.target libc.src.__support.OSUtil.linux.loader_services
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  startup
  SRCS startup.cpp
  HDRS startup.h
  DEPENDS .initialization .entry .graph .io .arena .unwind_images .runtime .mmix.target .mmix.syscall
    libc.hdr.errno_macros libc.hdr.fcntl_macros libc.include.sys_syscall
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  initialization
  SRCS initialization.cpp
  HDRS initialization.h
  DEPENDS .initialization_plan .graph .entry .arena .mmix.target libc.hdr.errno_macros
    libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  protection
  SRCS protection.cpp
  HDRS protection.h
  DEPENDS .elf .mmix.target .mmix.syscall libc.hdr.errno_macros
    libc.hdr.sys_mman_macros libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  relocation
  SRCS relocation.cpp
  HDRS relocation.h
  DEPENDS .symbols .dynamic .elf .self_relocation .mmix.target .tls_layout
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  symbols
  SRCS symbols.cpp
  HDRS symbols.h
  DEPENDS .dynamic .elf .arena .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  graph
  SRCS graph.cpp
  HDRS graph.h
  DEPENDS .search .mapping .io .arena .symbols .relocation .protection libc.hdr.errno_macros
    libc.include.sys_syscall libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  search
  SRCS search.cpp
  HDRS search.h
  DEPENDS .mapping libc.hdr.errno_macros
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  mapping
  SRCS mapping.cpp
  HDRS mapping.h
  DEPENDS .dynamic .elf .arena .io .mmix.target .mmix.syscall
    libc.hdr.errno_macros libc.hdr.stdio_macros libc.hdr.sys_mman_macros
    libc.include.sys_syscall libc.hdr.func.free libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  dynamic
  SRCS dynamic.cpp
  HDRS dynamic.h
  DEPENDS .elf .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  self_relocation
  SRCS self_relocation.cpp
  HDRS self_relocation.h
  DEPENDS .elf .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  entry
  SRCS entry.cpp
  HDRS entry.h
  DEPENDS .elf .mmix.target
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  elf
  SRCS elf.cpp
  HDRS elf.h
  DEPENDS .mmix.target libc.hdr.elf_proxy libc.src.__support.CPP.span
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  io
  SRCS io.cpp
  HDRS io.h syscall.h
  DEPENDS
    .mmix.syscall
    libc.hdr.types.size_t
    libc.hdr.errno_macros
    libc.hdr.fcntl_macros
    libc.hdr.sys_stat_macros
    libc.include.sys_syscall
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)

add_object_library(
  arena
  SRCS arena.cpp
  HDRS arena.h
  DEPENDS
    .mmix.syscall
    libc.hdr.types.size_t
    libc.hdr.errno_macros
    libc.hdr.sys_mman_macros
    libc.include.sys_syscall
    # Only placement new is used; do not include the allocating new.cpp provider.
    libc.hdr.func.free
    libc.src.__support.common
  COMPILE_OPTIONS ${LIBC_LOADER_COMPILE_OPTIONS}
)
