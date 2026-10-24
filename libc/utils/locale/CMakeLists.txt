enable_testing()
add_test(
  NAME locale_monetary_category_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_monetary_category.py
)
add_test(
  NAME locale_numeric_category_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_numeric_category.py
)
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
  NAME locale_ctype_source_test
  COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_CURRENT_BINARY_DIR}
    ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_ctype_source.py
)
add_test(
  NAME locale_simple_categories_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_simple_categories.py
)
