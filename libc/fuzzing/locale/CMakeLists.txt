add_libc_fuzzer(
  locale_data_fuzz
  SRCS locale_data_fuzz.cpp
  DEPENDS libc.src.locale.locale_data libc.src.locale.numeric_data
    libc.src.locale.ctype_data libc.src.locale.monetary_data
)
