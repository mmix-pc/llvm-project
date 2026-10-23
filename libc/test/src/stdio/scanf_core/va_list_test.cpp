//===-- Scanf caller va_list ownership tests ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/arg_list.h"
#include "src/stdio/fclose.h"
#include "src/stdio/fmemopen.h"
#include "src/stdio/scanf_core/vfscanf_internal.h"
#include "src/stdio/stdin.h"
#include "test/UnitTest/Test.h"

static unsigned caller_list_ends;
// The private ArgList implementation is already parsed. Instrument only the
// entrypoint's va_end, including on ABIs where va_end has no observable effect.
#pragma push_macro("va_end")
#undef va_end
#define va_end(list) (++caller_list_ends, __builtin_va_end(list))
#include "src/stdio/generic/vfscanf.cpp"
#include "src/stdio/generic/vscanf.cpp"
#pragma pop_macro("va_end")

static int scan(::FILE *file, bool standard_input, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = standard_input ? LIBC_NAMESPACE::vscanf(format, args)
                              : LIBC_NAMESPACE::vfscanf(file, format, args);
  va_end(args);
  return result;
}

TEST(LlvmLibcScanfVaListTest, EntrypointsDoNotEndCallerLists) {
  for (unsigned iteration = 0; iteration < 2; ++iteration) {
    bool standard_input = iteration != 0;
    char text[] = "42";
    ::FILE *file = LIBC_NAMESPACE::fmemopen(text, 2, "r");
    ASSERT_NE(file, nullptr);
    ::FILE *saved = LIBC_NAMESPACE::stdin;
    LIBC_NAMESPACE::stdin = file;
    int value = 0;
    caller_list_ends = 0;
    EXPECT_EQ(scan(file, standard_input, "%d", &value), 1);
    EXPECT_EQ(value, 42);
    EXPECT_EQ(scan(file, standard_input, "%d", &value), EOF);
    EXPECT_EQ(caller_list_ends, 0U);
    LIBC_NAMESPACE::stdin = saved;
    EXPECT_EQ(LIBC_NAMESPACE::fclose(file), 0);
  }
}
