cmake_minimum_required(VERSION 3.20)
project(MMIXExceptionOwners LANGUAGES CXX)

# Native execution of the real ABI entrypoints and MMIX storage policy. This
# does not qualify the target loader or MMIX instruction execution.
if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Exception owner tests require native Linux Clang")
endif()
set(MMIX_NATIVE_RUNTIME "" CACHE PATH "Native LLVM libc++/libc++abi build")
foreach(input include/c++/v1/__config_site lib/libc++.a lib/libc++abi.a)
  if(NOT EXISTS "${MMIX_NATIVE_RUNTIME}/${input}")
    message(FATAL_ERROR "Missing native runtime input: ${input}")
  endif()
endforeach()
get_filename_component(source "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_compile_options(-nostdinc++ -UNDEBUG)
include_directories("${MMIX_NATIVE_RUNTIME}/include/c++/v1"
  "${source}/include" "${source}/../libunwind/include"
  "${source}/../libcxx/src")
set(CMAKE_CXX_STANDARD 23)
add_library(exception-owner-payload MODULE exception_owner_payload.cpp)
target_link_options(exception-owner-payload PRIVATE -nostdlib++)
add_executable(exception-owners exception_owners.cpp
  "${source}/src/cxa_exception.cpp"
  "${source}/src/mmix/ExceptionStorage.cpp")
target_compile_definitions(exception-owners PRIVATE
  _LIBCXXABI_MMIX_EXCEPTION_OWNERS LIBCXX_BUILDING_LIBCXXABI
  _LIBCPP_BUILDING_LIBRARY _LIBCXXABI_BUILDING_LIBRARY)
target_link_options(exception-owners PRIVATE -nostdlib++ -Wl,--export-dynamic)
target_link_libraries(exception-owners PRIVATE
  "${MMIX_NATIVE_RUNTIME}/lib/libc++.a"
  "${MMIX_NATIVE_RUNTIME}/lib/libc++abi.a" dl pthread)
enable_testing()
add_test(NAME exception-owners COMMAND exception-owners
  $<TARGET_FILE:exception-owner-payload>)
set_tests_properties(exception-owners PROPERTIES TIMEOUT 30)
