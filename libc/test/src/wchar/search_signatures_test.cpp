//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/wchar/wcschr.h"
#include "src/wchar/wcspbrk.h"
#include "src/wchar/wcsrchr.h"
#include "src/wchar/wcsstr.h"
#include "src/wchar/wmemchr.h"
#include "test/UnitTest/Test.h"

// C search functions accept const input but return a mutable pointer. The
// caller may only modify the result when the original object is mutable.
TEST(LlvmLibcWideSearchSignatures, MutableResults) {
  wchar_t *(*first)(const wchar_t *, wchar_t) = LIBC_NAMESPACE::wcschr;
  wchar_t *(*last)(const wchar_t *, wchar_t) = LIBC_NAMESPACE::wcsrchr;
  wchar_t *(*any)(const wchar_t *, const wchar_t *) = LIBC_NAMESPACE::wcspbrk;
  wchar_t *(*substring)(const wchar_t *, const wchar_t *) =
      LIBC_NAMESPACE::wcsstr;
  wchar_t *(*memory)(const wchar_t *, wchar_t, size_t) =
      LIBC_NAMESPACE::wmemchr;
  wchar_t text[] = L"aba";
  EXPECT_EQ(first(text, L'a'), text);
  EXPECT_EQ(last(text, L'a'), text + 2);
  EXPECT_EQ(any(text, L"b"), text + 1);
  EXPECT_EQ(substring(text, L"ba"), text + 1);
  EXPECT_EQ(memory(text, L'b', 3), text + 1);
  *first(text, L'b') = L'c';
  EXPECT_EQ(text[1], L'c');
}
