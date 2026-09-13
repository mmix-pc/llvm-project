#if defined(TEST_HALF)
_Float16 value;
#elif defined(TEST_WIDE_ABI)
_BitInt(128) wide(_BitInt(128) value) { return value; }
#elif defined(TEST_ADDRESS_SPACE)
typedef int __attribute__((address_space(1))) as1_int;
as1_int *address_space(as1_int *value) { return value; }
#else
long identity(long value) { return value; }
#endif
