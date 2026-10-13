cmake_minimum_required(VERSION 3.20.0)
project(LLVMLibCLoaderTests LANGUAGES CXX)

# These tests exercise loader components on the host, not target startup.
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Loader host tests require a native Linux Clang compiler")
endif()
enable_testing()
include(../CompileOptions.cmake)
add_executable(loader-target-test target_test.cpp ../linux/mmix/target.cpp)
target_include_directories(loader-target-test PRIVATE ../..)
target_compile_definitions(loader-target-test PRIVATE LIBC_NAMESPACE=__llvm_libc)
target_compile_options(loader-target-test PRIVATE ${LIBC_LOADER_COMPILE_OPTIONS})
target_compile_features(loader-target-test PRIVATE cxx_std_17)
add_test(NAME loader-target COMMAND loader-target-test)
add_test(NAME loader-configuration
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/config_test.cmake)

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
