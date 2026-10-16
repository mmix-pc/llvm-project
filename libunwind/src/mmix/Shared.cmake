# The loader adopts one process-resident service before callbacks or workers.
# The same service searches both startup images and later published snapshots.
function(mmix_configure_shared_unwind)
  if(NOT TARGET unwind_shared_objects OR NOT TARGET unwind_shared)
    message(FATAL_ERROR "MMIX shared unwind requires the upstream shared targets")
  endif()
  target_sources(unwind_shared_objects PRIVATE
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/StartupImages.cpp"
    "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CursorLifetime.cpp")
  set(exports "${CMAKE_CURRENT_BINARY_DIR}/mmix-shared-unwind.exports")
  file(WRITE "${exports}"
    "{ global: _Unwind_*; unw_*; __gcc_personality_v0; __libunwind_mmix_adopt_startup_images; __libunwind_mmix_cursor_begin; __libunwind_mmix_cursor_end; __libunwind_mmix_cursor_finish; __libunwind_mmix_set_managed_stop; local: *; };\n")
  set_property(TARGET unwind_shared APPEND PROPERTY LINK_OPTIONS
    "LINKER:--version-script=${exports}")
  set_property(TARGET unwind_shared APPEND PROPERTY LINK_DEPENDS "${exports}")
endfunction()

cmake_language(DEFER CALL mmix_configure_shared_unwind)
