cmake_minimum_required(VERSION 3.20.0)
project(LLVMLibCLoaderTests LANGUAGES CXX)

# These tests exercise loader components on the host, not target startup.
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Loader host tests require a native Linux Clang compiler")
endif()
enable_testing()
find_package(Threads REQUIRED)
include(../CompileOptions.cmake)

# Match the interpreter's no-personality frames when callbacks unwind into the
# libc caller. The API fixture and libc wrapper retain native exception support.
set_source_files_properties(../linux/runtime_api.cpp PROPERTIES
  COMPILE_OPTIONS "-fno-exceptions;-funwind-tables")

add_executable(loader-module-ownership-test module_ownership_test.cpp
  ../linux/arena.cpp)
target_include_directories(loader-module-ownership-test PRIVATE ../..)
target_compile_definitions(loader-module-ownership-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-module-ownership-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-module-ownership-test PRIVATE cxx_std_17)
target_link_libraries(loader-module-ownership-test PRIVATE Threads::Threads)
add_test(NAME loader-module-ownership COMMAND loader-module-ownership-test)
set_tests_properties(loader-module-ownership PROPERTIES TIMEOUT 30)

add_executable(loader-runtime-initialization-test runtime_initialization_test.cpp
  ../linux/runtime_initialization.cpp ../linux/locks.cpp
  ../linux/arena.cpp ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-runtime-initialization-test PRIVATE ../..)
target_compile_definitions(loader-runtime-initialization-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-runtime-initialization-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-runtime-initialization-test PRIVATE cxx_std_17)
target_link_libraries(loader-runtime-initialization-test PRIVATE Threads::Threads)
add_test(NAME loader-runtime-initialization COMMAND loader-runtime-initialization-test)
set_tests_properties(loader-runtime-initialization PROPERTIES TIMEOUT 30)

add_executable(loader-runtime-unwind-test runtime_unwind_test.cpp
  ../linux/runtime_unwind.cpp ../linux/runtime_scopes.cpp ../linux/unwind_images.cpp
  ../linux/image_registry.cpp ../linux/tls_layout.cpp ../linux/symbols.cpp
  ../linux/arena.cpp ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-runtime-unwind-test PRIVATE ../..)
target_compile_definitions(loader-runtime-unwind-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-runtime-unwind-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-runtime-unwind-test PRIVATE cxx_std_17)
target_link_libraries(loader-runtime-unwind-test PRIVATE Threads::Threads)
add_test(NAME loader-runtime-unwind COMMAND loader-runtime-unwind-test)
set_tests_properties(loader-runtime-unwind PROPERTIES TIMEOUT 30)

add_executable(loader-unwind-cursor-test unwind_cursor_test.cpp
  ../../../libunwind/src/mmix/CursorLifetime.cpp
  ../../../libunwind/src/mmix/StartupImages.cpp)
target_compile_features(loader-unwind-cursor-test PRIVATE cxx_std_17)
target_link_libraries(loader-unwind-cursor-test PRIVATE Threads::Threads)
add_test(NAME loader-unwind-cursor COMMAND loader-unwind-cursor-test)
set_tests_properties(loader-unwind-cursor PROPERTIES TIMEOUT 30)

add_executable(loader-thread-tls-test thread_tls_test.cpp
  ../linux/thread_tls.cpp ../linux/runtime_tls.cpp ../linux/dtv.cpp
  ../linux/tls_allocation.cpp ../linux/tls_layout.cpp ../linux/runtime_scopes.cpp
  ../linux/image_registry.cpp ../linux/arena.cpp ../linux/symbols.cpp
  ../linux/relocation.cpp ../linux/dynamic.cpp ../linux/elf.cpp ../linux/io.cpp
  ../linux/mmix/target.cpp)
target_include_directories(loader-thread-tls-test PRIVATE ../..)
target_compile_definitions(loader-thread-tls-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-thread-tls-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS} -fno-pie)
target_link_options(loader-thread-tls-test PRIVATE -no-pie)
target_compile_features(loader-thread-tls-test PRIVATE cxx_std_17)
add_test(NAME loader-thread-tls COMMAND loader-thread-tls-test)
set_tests_properties(loader-thread-tls PROPERTIES TIMEOUT 30)

add_executable(loader-runtime-tls-test runtime_tls_test.cpp
  ../linux/runtime_tls.cpp ../linux/dtv.cpp ../linux/tls_layout.cpp
  ../linux/elf.cpp ../linux/io.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-runtime-tls-test PRIVATE ../..)
target_compile_definitions(loader-runtime-tls-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-runtime-tls-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS} -fno-pie)
target_link_options(loader-runtime-tls-test PRIVATE -no-pie)
target_compile_features(loader-runtime-tls-test PRIVATE cxx_std_17)
add_test(NAME loader-runtime-tls COMMAND loader-runtime-tls-test)
set_tests_properties(loader-runtime-tls PROPERTIES TIMEOUT 30)

add_executable(loader-dtv-test dtv_test.cpp ../linux/dtv.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-dtv-test PRIVATE ../..)
target_compile_definitions(loader-dtv-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-dtv-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-dtv-test PRIVATE cxx_std_17)
target_link_libraries(loader-dtv-test PRIVATE Threads::Threads)
add_test(NAME loader-dtv COMMAND loader-dtv-test)
set_tests_properties(loader-dtv PROPERTIES TIMEOUT 30)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  add_executable(loader-runtime-api-test runtime_api_test.cpp
    ../linux/runtime_api.cpp ../linux/runtime_queries.cpp ../linux/runtime_initialization.cpp ../linux/locks.cpp
    ../linux/runtime_load.cpp ../linux/runtime_unwind.cpp ../linux/unwind_images.cpp
    ../linux/runtime_graph.cpp ../linux/runtime_scopes.cpp ../linux/image_registry.cpp
    ../linux/relocation.cpp ../linux/protection.cpp ../linux/symbols.cpp
    ../linux/search.cpp ../linux/mapping.cpp ../linux/io.cpp ../linux/arena.cpp
    ../linux/tls_layout.cpp ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp
    ../../src/dlfcn/linux/mmix/loading.cpp ../../src/dlfcn/linux/mmix/dlopen.cpp
    ../../src/dlfcn/linux/mmix/dlsym.cpp ../../src/dlfcn/linux/mmix/dlerror.cpp
    ../../src/dlfcn/linux/mmix/dlclose.cpp ../../src/dlfcn/linux/mmix/dladdr.cpp
    ../../src/dlfcn/linux/mmix/dlinfo.cpp ../../src/dlfcn/linux/mmix/dl_iterate_phdr.cpp)
  target_include_directories(loader-runtime-api-test PRIVATE ../..)
  target_compile_definitions(loader-runtime-api-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
  target_compile_features(loader-runtime-api-test PRIVATE cxx_std_17)
  target_link_libraries(loader-runtime-api-test PRIVATE Threads::Threads)
  # Takes real cross-linked MMIX DSOs; never executes their code on the host.
  add_executable(loader-runtime-load-linked-test runtime_load_linked_test.cpp
    ../linux/runtime_unwind.cpp ../linux/unwind_images.cpp
    ../linux/runtime_load.cpp ../linux/runtime_graph.cpp ../linux/runtime_scopes.cpp
    ../linux/image_registry.cpp ../linux/relocation.cpp ../linux/protection.cpp
    ../linux/symbols.cpp ../linux/search.cpp ../linux/mapping.cpp ../linux/io.cpp
    ../linux/arena.cpp ../linux/tls_layout.cpp ../linux/dynamic.cpp ../linux/elf.cpp
    ../linux/mmix/target.cpp)
  target_include_directories(loader-runtime-load-linked-test PRIVATE ../..)
  target_compile_definitions(loader-runtime-load-linked-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
  target_compile_features(loader-runtime-load-linked-test PRIVATE cxx_std_17)
endif()
add_executable(loader-runtime-scopes-test runtime_scopes_test.cpp
  ../linux/runtime_scopes.cpp ../linux/image_registry.cpp ../linux/symbols.cpp
  ../linux/dynamic.cpp ../linux/elf.cpp ../linux/arena.cpp ../linux/tls_layout.cpp
  ../linux/mmix/target.cpp)
target_include_directories(loader-runtime-scopes-test PRIVATE ../..)
target_compile_definitions(loader-runtime-scopes-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-runtime-scopes-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-runtime-scopes-test PRIVATE cxx_std_17)
target_link_libraries(loader-runtime-scopes-test PRIVATE Threads::Threads)
add_test(NAME loader-runtime-scopes COMMAND loader-runtime-scopes-test)
set_tests_properties(loader-runtime-scopes PROPERTIES TIMEOUT 30)
add_executable(loader-locks-test locks_test.cpp ../linux/locks.cpp ../linux/arena.cpp)
target_include_directories(loader-locks-test PRIVATE ../..)
target_compile_definitions(loader-locks-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-locks-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-locks-test PRIVATE cxx_std_17)
target_link_libraries(loader-locks-test PRIVATE Threads::Threads)
add_test(NAME loader-locks COMMAND loader-locks-test)
set_tests_properties(loader-locks PROPERTIES TIMEOUT 30)
add_executable(loader-image-registry-test image_registry_test.cpp
  ../linux/image_registry.cpp ../linux/dynamic.cpp ../linux/arena.cpp ../linux/tls_layout.cpp
  ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-image-registry-test PRIVATE ../..)
target_compile_definitions(loader-image-registry-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-image-registry-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-image-registry-test PRIVATE cxx_std_17)
add_test(NAME loader-image-registry COMMAND loader-image-registry-test)
set_tests_properties(loader-image-registry PROPERTIES TIMEOUT 30)
add_executable(loader-unwind-images-test unwind_images_test.cpp
  ../linux/unwind_images.cpp ../linux/arena.cpp ../linux/elf.cpp
  ../linux/mmix/target.cpp)
target_include_directories(loader-unwind-images-test PRIVATE ../..)
target_compile_definitions(loader-unwind-images-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_features(loader-unwind-images-test PRIVATE cxx_std_17)
add_test(NAME loader-unwind-images COMMAND loader-unwind-images-test)
set_tests_properties(loader-unwind-images PROPERTIES TIMEOUT 30)
add_executable(loader-tls-resolver-test tls_resolver_test.cpp
  ../linux/tls_resolver.cpp ../linux/tls_allocation.cpp ../linux/tls_layout.cpp
  ../linux/relocation.cpp ../linux/symbols.cpp ../linux/dynamic.cpp
  ../linux/elf.cpp ../linux/mmix/target.cpp ../linux/io.cpp
  ../../src/__support/OSUtil/linux/mmix/dynamic_tls.cpp)
target_include_directories(loader-tls-resolver-test PRIVATE ../..)
# Do not interpose the host dynamic linker's TLS entry point.
target_compile_definitions(loader-tls-resolver-test PRIVATE
  LIBC_NAMESPACE=__llvm_libc __tls_get_addr=mmix_test_tls_get_addr)
target_compile_options(loader-tls-resolver-test PRIVATE -fno-pie)
target_link_options(loader-tls-resolver-test PRIVATE -no-pie)
target_link_libraries(loader-tls-resolver-test PRIVATE Threads::Threads)
target_compile_features(loader-tls-resolver-test PRIVATE cxx_std_17)
add_test(NAME loader-tls-resolver COMMAND loader-tls-resolver-test)
set_tests_properties(loader-tls-resolver PROPERTIES TIMEOUT 30)
add_executable(loader-target-test target_test.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-target-test PRIVATE ../..)
target_compile_definitions(loader-target-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-target-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-target-test PRIVATE cxx_std_17)
add_test(NAME loader-target COMMAND loader-target-test)
add_test(NAME loader-configuration
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/config_test.cmake)
add_test(NAME loader-loading-configuration
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/dlfcn_config_test.cmake)

add_executable(loader-support-test support_test.cpp ../linux/io.cpp
  ../linux/arena.cpp)
target_include_directories(loader-support-test PRIVATE ../..)
target_compile_definitions(loader-support-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-support-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-support-test PRIVATE cxx_std_17)
add_test(NAME loader-support COMMAND loader-support-test)
set_tests_properties(loader-support PROPERTIES TIMEOUT 30)

add_executable(loader-elf-test elf_test.cpp ../linux/elf.cpp
  ../linux/mmix/target.cpp)
target_include_directories(loader-elf-test PRIVATE ../..)
target_compile_definitions(loader-elf-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-elf-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-elf-test PRIVATE cxx_std_17)
add_test(NAME loader-elf COMMAND loader-elf-test)
set_tests_properties(loader-elf PROPERTIES TIMEOUT 30)

add_executable(loader-entry-test entry_test.cpp ../linux/entry.cpp
  ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-entry-test PRIVATE ../..)
target_compile_definitions(loader-entry-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-entry-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-entry-test PRIVATE cxx_std_17)
add_test(NAME loader-entry COMMAND loader-entry-test)
set_tests_properties(loader-entry PROPERTIES TIMEOUT 30)

add_executable(loader-self-relocation-test self_relocation_test.cpp
  ../linux/self_relocation.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-self-relocation-test PRIVATE ../..)
target_compile_definitions(loader-self-relocation-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-self-relocation-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-self-relocation-test PRIVATE cxx_std_17)
add_test(NAME loader-self-relocation COMMAND loader-self-relocation-test)
set_tests_properties(loader-self-relocation PROPERTIES TIMEOUT 30)

# Supplied a real cross-linked fixture by the artifact validation runner.
# This validates mapped bytes on the host; it never executes target code.
add_executable(loader-linked-image-test linked_image_test.cpp
  ../linux/dynamic.cpp ../linux/symbols.cpp ../linux/relocation.cpp ../linux/tls_layout.cpp ../linux/self_relocation.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)

add_executable(loader-relocation-test relocation_test.cpp ../linux/relocation.cpp
  ../linux/tls_layout.cpp ../linux/symbols.cpp ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-relocation-test PRIVATE ../..)
target_compile_definitions(loader-relocation-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-relocation-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-relocation-test PRIVATE cxx_std_17)
add_test(NAME loader-relocation COMMAND loader-relocation-test)
set_tests_properties(loader-relocation PROPERTIES TIMEOUT 30)
target_include_directories(loader-linked-image-test PRIVATE ../..)
target_compile_definitions(loader-linked-image-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_features(loader-linked-image-test PRIVATE cxx_std_17)

add_executable(loader-dynamic-test dynamic_test.cpp ../linux/dynamic.cpp
  ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-dynamic-test PRIVATE ../..)
target_compile_definitions(loader-dynamic-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-dynamic-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-dynamic-test PRIVATE cxx_std_17)
add_test(NAME loader-dynamic COMMAND loader-dynamic-test)
set_tests_properties(loader-dynamic PROPERTIES TIMEOUT 30)

add_executable(loader-tls-test tls_test.cpp ../linux/dynamic.cpp
  ../linux/symbols.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-tls-test PRIVATE ../..)
target_compile_definitions(loader-tls-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_features(loader-tls-test PRIVATE cxx_std_17)
add_test(NAME loader-tls COMMAND loader-tls-test)
set_tests_properties(loader-tls PROPERTIES TIMEOUT 30)

add_executable(loader-tls-layout-test tls_layout_test.cpp ../linux/tls_layout.cpp
  ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-tls-layout-test PRIVATE ../..)
target_compile_definitions(loader-tls-layout-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-tls-layout-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-tls-layout-test PRIVATE cxx_std_17)
add_test(NAME loader-tls-layout COMMAND loader-tls-layout-test)
set_tests_properties(loader-tls-layout PROPERTIES TIMEOUT 30)

add_executable(loader-tls-runtime-test tls_runtime_test.cpp
  ../linux/tls_allocation.cpp ../linux/tls_layout.cpp ../linux/relocation.cpp
  ../linux/symbols.cpp ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-tls-runtime-test PRIVATE ../..)
target_compile_definitions(loader-tls-runtime-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-tls-runtime-test PRIVATE -fno-pie)
target_link_options(loader-tls-runtime-test PRIVATE -no-pie)
target_compile_features(loader-tls-runtime-test PRIVATE cxx_std_17)
add_test(NAME loader-tls-runtime COMMAND loader-tls-runtime-test)
set_tests_properties(loader-tls-runtime PROPERTIES TIMEOUT 30)

# Supplied real main/DSO pairs by the focused TLS artifact runner.
add_executable(loader-tls-linked-test tls_linked_test.cpp
  ../linux/tls_layout.cpp ../linux/relocation.cpp ../linux/symbols.cpp
  ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-tls-linked-test PRIVATE ../..)
target_compile_definitions(loader-tls-linked-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_features(loader-tls-linked-test PRIVATE cxx_std_17)

add_executable(loader-mapping-test mapping_test.cpp ../linux/mapping.cpp
  ../linux/io.cpp ../linux/arena.cpp ../linux/dynamic.cpp ../linux/elf.cpp
  ../linux/mmix/target.cpp)
target_include_directories(loader-mapping-test PRIVATE ../..)
target_compile_definitions(loader-mapping-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-mapping-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS}
  -UNDEBUG -fno-pie)
target_link_options(loader-mapping-test PRIVATE -no-pie)
target_compile_features(loader-mapping-test PRIVATE cxx_std_17)
add_test(NAME loader-mapping COMMAND loader-mapping-test)
set_tests_properties(loader-mapping PROPERTIES TIMEOUT 30)

add_executable(loader-search-test search_test.cpp ../linux/search.cpp
  ../linux/mapping.cpp ../linux/io.cpp ../linux/arena.cpp ../linux/dynamic.cpp
  ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-search-test PRIVATE ../..)
target_compile_definitions(loader-search-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-search-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS} -UNDEBUG)
target_compile_features(loader-search-test PRIVATE cxx_std_17)
add_test(NAME loader-search COMMAND loader-search-test)
set_tests_properties(loader-search PROPERTIES TIMEOUT 30)

add_executable(loader-graph-test graph_test.cpp ../linux/graph.cpp
  ../linux/runtime_state.cpp ../linux/runtime_api.cpp ../linux/runtime_queries.cpp
  ../linux/runtime_initialization.cpp ../linux/locks.cpp
  ../linux/thread_tls.cpp ../linux/runtime_tls.cpp ../linux/dtv.cpp
  ../linux/runtime_load.cpp ../linux/runtime_scopes.cpp
  ../linux/runtime_unwind.cpp
  ../linux/runtime_graph.cpp ../linux/image_registry.cpp
  ../linux/tls_layout.cpp
  ../linux/runtime.cpp ../linux/tls_allocation.cpp ../linux/tls_resolver.cpp
  ../linux/startup.cpp ../linux/entry.cpp ../linux/unwind_images.cpp
  ../linux/initialization.cpp ../linux/relocation.cpp ../linux/protection.cpp ../linux/symbols.cpp ../linux/search.cpp ../linux/mapping.cpp ../linux/io.cpp ../linux/arena.cpp
  ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-graph-test PRIVATE ../..)
target_compile_definitions(loader-graph-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-graph-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS}
  -UNDEBUG -fno-pie)
target_link_options(loader-graph-test PRIVATE -no-pie)
target_compile_features(loader-graph-test PRIVATE cxx_std_17)
add_test(NAME loader-graph COMMAND loader-graph-test)
set_tests_properties(loader-graph PROPERTIES TIMEOUT 30)

add_executable(loader-symbols-test symbols_test.cpp ../linux/symbols.cpp
  ../linux/dynamic.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-symbols-test PRIVATE ../..)
target_compile_definitions(loader-symbols-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-symbols-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-symbols-test PRIVATE cxx_std_17)
add_test(NAME loader-symbols COMMAND loader-symbols-test)
set_tests_properties(loader-symbols PROPERTIES TIMEOUT 30)

add_executable(loader-protection-test protection_test.cpp ../linux/protection.cpp
  ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-protection-test PRIVATE ../..)
target_compile_definitions(loader-protection-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-protection-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-protection-test PRIVATE cxx_std_17)
add_test(NAME loader-protection COMMAND loader-protection-test)
set_tests_properties(loader-protection PROPERTIES TIMEOUT 30)

add_executable(loader-protection-fault-test protection_fault_test.cpp
  ../linux/protection.cpp ../linux/elf.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-protection-fault-test PRIVATE ../..)
target_compile_definitions(loader-protection-fault-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_features(loader-protection-fault-test PRIVATE cxx_std_17)
add_test(NAME loader-protection-fault COMMAND loader-protection-fault-test)
set_tests_properties(loader-protection-fault PROPERTIES TIMEOUT 30)
