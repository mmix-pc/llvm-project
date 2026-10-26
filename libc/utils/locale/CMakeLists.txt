enable_testing()
add_test(
  NAME locale_validation_profile_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_validation_locale.py
)
add_test(
  NAME locale_width_table_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_width_table.py
)
add_test(
  NAME locale_builtin_transliteration_test
  COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_CURRENT_BINARY_DIR}
    ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_builtin_transliteration.py
)
add_test(
  NAME locale_regional_test
  COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_CURRENT_BINARY_DIR}
    ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_regional_locale.py
)
add_test(
  NAME locale_collation_category_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_collation_category.py
)
add_test(
  NAME locale_collation_source_test
  COMMAND ${CMAKE_COMMAND} -E env TMPDIR=${CMAKE_CURRENT_BINARY_DIR}
    ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_collation_source.py
)
add_test(
  NAME locale_transliteration_category_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_transliteration_category.py
)
add_test(
  NAME locale_encoding_source_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_encoding_source.py
)
add_test(
  NAME locale_messages_category_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_messages_category.py
)
add_test(
  NAME locale_time_category_test
  COMMAND ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/test_time_category.py
)
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
