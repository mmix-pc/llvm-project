// REQUIRES: mmix-registered-target, lld
// RUN: split-file %s %t
// RUN: %python %S/Inputs/linux-static-elf.py %t llvm-ar llvm-readobj %clang

// Runtime-free link inputs, not a CRT or an executable startup implementation.
// The host checker inspects output without executing any MMIX code.

//--- entry.c
extern long via_cpp(long);
extern long ir_use(long);
volatile long sink;

void _start(void) {
  sink = via_cpp(7) + ir_use(9);
  for (;;)
    __asm__ volatile("");
}

//--- provider.c
long values[3] = {0x1122, 0x3344, 0x5566};
long bss_words[8];
__attribute__((noinline)) long leaf(long x) { return x + values[1]; }
long *volatile data_ptr = values + 1;
long (*volatile fn_ptr)(long) = leaf;
long unused_selected(void) { return 777; }

//--- consumer.cpp
extern "C" {
extern long *volatile data_ptr;
extern long (*volatile fn_ptr)(long);
}
struct Reader {
  template <int Bias> static long apply(long x) {
    return fn_ptr(x) + *data_ptr + Bias;
  }
};
extern "C" long via_cpp(long x) { return Reader::apply<3>(x); }

//--- unused.c
extern long unavailable(void);
long unextracted(void) { return unavailable(); }

//--- consumer.ll
@values = external global [3 x i64]
@bss_words = external global [8 x i64]
@ir_ptr = global ptr getelementptr ([3 x i64], ptr @values, i64 0, i64 2)

define i64 @ir_use(i64 %x) {
  store i64 %x, ptr @bss_words
  %p = load ptr, ptr @ir_ptr
  %v = load i64, ptr %p
  %r = add i64 %v, %x
  ret i64 %r
}
