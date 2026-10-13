cmake_minimum_required(VERSION 3.20.0)
project(LLVMLibCLoaderTests LANGUAGES CXX)

# These tests exercise decoded ELF policy on the host, not target startup.
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
