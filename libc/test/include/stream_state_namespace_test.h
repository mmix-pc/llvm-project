//===-- Tests for stream state types and namespace isolation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// These names are not reserved by the ISO stream headers. A leaked typedef
// conflicts with the object declaration in both C and C++.
extern int off_t, uint8_t, uint32_t;

#include "include/llvm-libc-macros/wchar-macros.h"
#include "include/llvm-libc-types/fpos_t.h"

#ifdef __cplusplus
#define CHECK static_assert
#define ALIGNOF alignof
#define SAME_TYPE(a, b) __is_same(decltype(a), decltype(b))
#else
#define CHECK _Static_assert
#define ALIGNOF _Alignof
#define SAME_TYPE(a, b) __builtin_types_compatible_p(__typeof__(a), __typeof__(b))
#endif

CHECK(WCHAR_MAX == __WCHAR_MAX__, "wide character maximum");
CHECK(SAME_TYPE(WCHAR_MAX, +(__WCHAR_TYPE__)0), "promoted maximum type");
CHECK(SAME_TYPE(WCHAR_MIN, +(__WCHAR_TYPE__)0), "promoted minimum type");
#ifdef __WCHAR_MIN__
CHECK(WCHAR_MIN == __WCHAR_MIN__, "wide character minimum");
#elif defined(__WCHAR_UNSIGNED__)
CHECK(WCHAR_MIN == 0, "unsigned wide character minimum");
#else
CHECK(WCHAR_MIN == -__WCHAR_MAX__ - 1, "signed wide character minimum");
#endif

struct PreviousState {
  __UINT32_TYPE__ first;
  __UINT8_TYPE__ second;
  __UINT8_TYPE__ third;
};
struct PreviousPosition {
  __INT64_TYPE__ offset;
  struct PreviousState state;
};

CHECK(sizeof(mbstate_t) == sizeof(struct PreviousState), "state size");
CHECK(ALIGNOF(mbstate_t) == ALIGNOF(struct PreviousState), "state alignment");
CHECK(sizeof(fpos_t) == sizeof(struct PreviousPosition), "position size");
CHECK(ALIGNOF(fpos_t) == ALIGNOF(struct PreviousPosition), "position alignment");
CHECK(__builtin_offsetof(fpos_t, __state) ==
          __builtin_offsetof(struct PreviousPosition, state),
      "state offset");
CHECK(__builtin_offsetof(fpos_t, __offset) == 0, "file offset");
CHECK(__builtin_offsetof(mbstate_t, __field2) ==
          __builtin_offsetof(struct PreviousState, second),
      "second state field");
CHECK(__builtin_offsetof(mbstate_t, __field3) ==
          __builtin_offsetof(struct PreviousState, third),
      "third state field");
CHECK(SAME_TYPE(((fpos_t *)0)->__offset, (__INT64_TYPE__)0), "offset type");
CHECK(SAME_TYPE(((mbstate_t *)0)->__field1, (__UINT32_TYPE__)0), "first field type");
CHECK(SAME_TYPE(((mbstate_t *)0)->__field2, (__UINT8_TYPE__)0), "second field type");
CHECK(SAME_TYPE(((mbstate_t *)0)->__field3, (__UINT8_TYPE__)0), "third field type");

#ifdef __cplusplus
extern "C"
#endif
int main(void) { return 0; }
