enable_testing()
add_test(
  NAME locale_file_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_locale_file.py
)
add_custom_target(check-locale-file
  COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_CURRENT_BINARY_DIR}
    ${Python3_EXECUTABLE} -B -m unittest discover
    -s ${CMAKE_CURRENT_SOURCE_DIR} -p "test_*.py"
)
add_test(
  NAME locale_simple_categories_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_simple_categories.py
)
