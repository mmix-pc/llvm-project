include_guard(GLOBAL)

option(LIBUNWIND_MMIX_SIGNAL_CANCELLATION
  "Enable private MMIX signal cancellation bookkeeping and metadata policy" OFF)

# Retain the private compile-flag opt-ins used before the profile option.
if(NOT LIBUNWIND_MMIX_SIGNAL_CANCELLATION AND
   NOT "-D_LIBUNWIND_MMIX_CURSOR_LIFETIME" IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS AND
   NOT "-D_LIBUNWIND_USE_METADATA_POLICY" IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
  return()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "mmix" OR
   NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "mmix-unknown-linux")
  message(FATAL_ERROR "MMIX signal cancellation requires the MMIX Linux target")
endif()
foreach(option LIBUNWIND_ENABLE_THREADS LIBUNWIND_IS_NATIVE_ONLY
               LIBUNWIND_REMEMBER_HEAP_ALLOC)
  if(NOT ${option})
    message(FATAL_ERROR "MMIX signal cancellation requires ${option}=ON")
  endif()
endforeach()
foreach(option LIBUNWIND_IS_BAREMETAL LIBUNWIND_USE_FRAME_HEADER_CACHE)
  if(${option})
    message(FATAL_ERROR "MMIX signal cancellation rejects ${option}")
  endif()
endforeach()
if(NOT "-D_LIBUNWIND_MMIX_LINUX_THREADS" IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
  message(FATAL_ERROR "MMIX signal cancellation requires -D_LIBUNWIND_MMIX_LINUX_THREADS")
endif()

if(MMIX_LINUX_SHARED_RUNTIME_VALIDATED AND
   NOT MMIX_LINUX_THREADED_RUNTIME_VALIDATED AND
   LIBUNWIND_ENABLE_SHARED AND NOT LIBUNWIND_ENABLE_STATIC)
  if(NOT "-D_LIBUNWIND_MMIX_STARTUP_IMAGES" IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
    message(FATAL_ERROR "MMIX shared signal cancellation requires startup images")
  endif()
  # Shared.cmake owns the sources and exports. Set its selector before the
  # upstream object targets consume the additional compile flags.
  if(LIBUNWIND_MMIX_SIGNAL_CANCELLATION AND
     NOT "-D_LIBUNWIND_USE_METADATA_POLICY" IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
    list(APPEND LIBUNWIND_ADDITIONAL_COMPILE_FLAGS -D_LIBUNWIND_USE_METADATA_POLICY)
  endif()
elseif(MMIX_LINUX_THREADED_RUNTIME_VALIDATED AND
       NOT MMIX_LINUX_SHARED_RUNTIME_VALIDATED AND
       LIBUNWIND_ENABLE_STATIC AND NOT LIBUNWIND_ENABLE_SHARED)
  if("-D_LIBUNWIND_MMIX_STARTUP_IMAGES" IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
    message(FATAL_ERROR "MMIX static signal cancellation rejects startup images")
  endif()
else()
  message(FATAL_ERROR
    "MMIX signal cancellation requires a validated static pthread or shared-only runtime profile")
endif()

function(mmix_configure_signal_cancellation)
  if(MMIX_LINUX_SHARED_RUNTIME_VALIDATED)
    if(NOT TARGET unwind_shared_objects OR NOT TARGET unwind_shared)
      message(FATAL_ERROR "MMIX signal cancellation requires the upstream shared targets")
    endif()
    target_compile_definitions(unwind_shared_objects PRIVATE
      _LIBUNWIND_REMEMBER_REPLAY)
    return()
  endif()
  if(NOT TARGET unwind_static_objects OR NOT TARGET unwind_static)
    message(FATAL_ERROR "MMIX signal cancellation requires the upstream static targets")
  endif()
  # Static metadata needs ownership bookkeeping, but no loader image leases.
  target_sources(unwind_static_objects PRIVATE
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CursorLifetime.cpp")
  target_compile_definitions(unwind_static_objects PRIVATE
    _LIBUNWIND_MMIX_CURSOR_LIFETIME _LIBUNWIND_REMEMBER_REPLAY)
  if(LIBUNWIND_MMIX_SIGNAL_CANCELLATION OR
     "-D_LIBUNWIND_USE_METADATA_POLICY" IN_LIST LIBUNWIND_ADDITIONAL_COMPILE_FLAGS)
    target_sources(unwind_static_objects PRIVATE
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ForcedContext.cpp"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../UnwindMetadataPolicy.cpp")
    target_compile_definitions(unwind_static_objects PRIVATE
      _LIBUNWIND_USE_METADATA_POLICY)
  endif()
endfunction()

cmake_language(DEFER CALL mmix_configure_signal_cancellation)
