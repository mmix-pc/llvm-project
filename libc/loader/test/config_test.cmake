set(loader "${CMAKE_CURRENT_LIST_DIR}/../CMakeLists.txt")
execute_process(COMMAND "${CMAKE_COMMAND}" -P "${loader}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Disabled loader needs no target configuration: ${output}${error}")
endif()

foreach(platform IN ITEMS other-os other-arch)
  set(os ON)
  set(arch mmix)
  if(platform STREQUAL "other-os")
    set(os OFF)
  else()
    set(arch x86_64)
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -DLIBC_BUILD_LOADER=ON
    -DLIBC_TARGET_OS_IS_LINUX=${os} -DLIBC_TARGET_ARCHITECTURE=${arch}
    -P "${loader}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0 OR NOT error MATCHES "currently requires MMIX Linux")
    message(FATAL_ERROR "Unexpected ${platform} result: ${result}: ${output}${error}")
  endif()
endforeach()

foreach(flag IN ITEMS -pg -finstrument-functions -finstrument-functions-after-inlining)
  execute_process(COMMAND "${CMAKE_COMMAND}" -DCMAKE_CXX_FLAGS=${flag}
    -P "${CMAKE_CURRENT_LIST_DIR}/../CompileOptions.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0 OR NOT error MATCHES "does not support function instrumentation")
    message(FATAL_ERROR "Unexpected instrumentation result: ${result}: ${output}${error}")
  endif()
endforeach()
