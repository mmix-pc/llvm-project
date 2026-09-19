# Installation is opt-in and requires the complete Linux composition checks.
if(LIBCXX_MMIX_LINUX_INSTALL_RUNTIME AND NOT MMIX_LINUX_RUNTIME_VALIDATED)
  message(FATAL_ERROR "MMIX Linux installation requires validated runtime inputs")
endif()
if(NOT RUNTIMES_USE_LIBC STREQUAL "llvm-libc" OR
   NOT LIBCXX_ENABLE_THREADS OR NOT LIBCXX_HAS_EXTERNAL_THREAD_API OR
   LIBCXX_HAS_PTHREAD_API OR LIBCXX_HAS_C11_THREAD_API OR
   LIBCXX_HAS_WIN32_THREAD_API OR LIBCXX_ENABLE_SHARED OR
   ((LIBCXX_INSTALL_HEADERS OR LIBCXX_INSTALL_LIBRARY) AND
    NOT LIBCXX_MMIX_LINUX_INSTALL_RUNTIME))
  message(FATAL_ERROR "MMIX Linux libc++ requires non-installing static external-thread preparation with LLVM libc")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/linux/__external_threading"
  "${LIBCXX_GENERATED_INCLUDE_TARGET_DIR}/__external_threading" COPYONLY)
if(LIBCXX_MMIX_LINUX_INSTALL_RUNTIME)
  install(FILES "${LIBCXX_GENERATED_INCLUDE_TARGET_DIR}/__external_threading"
    DESTINATION "${LIBCXX_INSTALL_INCLUDE_TARGET_DIR}" COMPONENT cxx-headers)
  # The general cxx component also exports the unqualified experimental archive.
  install(FILES $<TARGET_FILE:cxx_static>
    DESTINATION "${LIBCXX_INSTALL_LIBRARY_DIR}" COMPONENT mmix-cxx-runtime)
endif()

# Independent preparation objects; complete cxx archive composition is separate.
if(TARGET cxx-headers)
  add_library(cxx_mmix_sync OBJECT "${CMAKE_CURRENT_LIST_DIR}/linux/Threading.cpp")
  target_link_libraries(cxx_mmix_sync PRIVATE runtimes-libc-headers)
  target_include_directories(cxx_mmix_sync PRIVATE "${LIBCXX_SOURCE_DIR}/../libc")
  target_compile_features(cxx_mmix_sync PRIVATE cxx_std_17)
  target_compile_options(cxx_mmix_sync PRIVATE -nostdinc++ -fno-exceptions -fno-rtti)

  add_library(cxx_mmix_thread_api OBJECT "${CMAKE_CURRENT_LIST_DIR}/linux/external_threading.cpp")
  target_link_libraries(cxx_mmix_thread_api PRIVATE cxx-headers)
  target_compile_features(cxx_mmix_thread_api PRIVATE cxx_std_23)
  target_compile_definitions(cxx_mmix_thread_api PRIVATE _LIBCPP_BUILDING_LIBRARY)
  if(LIBCXX_MMIX_LINUX_STATE_DIR)
    include("${CMAKE_CURRENT_LIST_DIR}/linux/RuntimeState.cmake")
  endif()
  if(LIBCXX_MMIX_LINUX_INSTALL_RUNTIME)
    foreach(target cxx_mmix_sync cxx_mmix_thread_api cxx_mmix_keys)
      if(NOT TARGET ${target})
        message(FATAL_ERROR "MMIX Linux installation requires ${target}")
      endif()
      list(APPEND LIBCXX_SOURCES $<TARGET_OBJECTS:${target}>)
    endforeach()
  endif()
endif()
