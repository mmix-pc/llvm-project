#if defined(TEST_HALF)
_Float16 value;
#elif defined(TEST_BITINT)
_BitInt(17) add_bitint(_BitInt(17) lhs, _BitInt(17) rhs) {
  return lhs + rhs;
}
#elif defined(TEST_COMPLEX)
_Complex int consume(_Complex int value) { return value; }
#elif defined(TEST_VECTOR)
typedef int int16 __attribute__((ext_vector_type(16)));
int16 value;
#elif defined(TEST_ATOMIC)
struct ThreeBytes {
  unsigned char bytes[3];
};
_Atomic(struct ThreeBytes) value;
#elif defined(TEST_ATOMIC_OPERATION)
float atomic_fetch_add(float *value) {
  return __atomic_fetch_add(value, 1.0f, __ATOMIC_SEQ_CST);
}
#elif defined(TEST_ATOMIC_RMW_OPERATOR)
float atomic_add_assign(_Atomic(float) *value) { return *value += 1.0f; }
#elif defined(TEST_ATOMIC_EXTENSION)
unsigned atomic_uinc(unsigned *value) {
  return __atomic_fetch_uinc(value, 1, __ATOMIC_SEQ_CST);
}
#elif defined(TEST_WIDE_ATOMIC)
struct SixteenBytes {
  unsigned char bytes[16];
};
_Atomic(struct SixteenBytes) value;
#elif defined(TEST_UNALIGNED_ATOMIC)
struct EightBytes {
  unsigned char bytes[8];
};
_Atomic(struct EightBytes) value;
#elif defined(TEST_OVERALIGNED_ARGUMENT)
struct __attribute__((aligned(16))) OverAligned {
  long words[2];
};
void consume(struct OverAligned value) {}
#elif defined(TEST_OVERALIGNED_RESULT)
struct __attribute__((aligned(16))) OverAligned {
  long words[3];
};
struct OverAligned produce(void) { return (struct OverAligned){0}; }
#elif defined(TEST_ADDRESS_SPACE)
int __attribute__((address_space(1))) value;
#elif defined(TEST_NAKED)
__attribute__((naked)) void naked_function(void) {}
#elif defined(TEST_TARGET_ATTRIBUTE)
__attribute__((target("base"))) void target_function(void) {}
#elif defined(TEST_MULTIVERSIONING)
__attribute__((target_clones("default", "base")))
void multiversioned_function(void) {}
#elif defined(TEST_CALLING_CONVENTION)
__attribute__((fastcall)) void alternate_calling_convention(void) {}
#elif defined(TEST_TLS)
_Thread_local int value;
#endif
