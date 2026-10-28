//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains a template class for testing reentrant qsort functions.
///
//===----------------------------------------------------------------------===//

#include "test/UnitTest/Test.h"

/// Provides shared tests for reentrant qsort variants.
///
/// The fixture verifies that a qsort-like implementation correctly handles
/// sorted data, reverse-sorted data, and comparators that dispatch through a
/// type-erased context pointer.
template <typename QsortFnTy, typename SizeTy>
class QsortReentrantTest : public LIBC_NAMESPACE::testing::Test {
private:
  static int int_compare_count(const void *l, const void *r, void *count_arg) {
    int li = *static_cast<const int *>(l);
    int ri = *static_cast<const int *>(r);
    SizeTy *count = static_cast<SizeTy *>(count_arg);
    *count = *count + 1;
    if (li == ri)
      return 0;
    if (li > ri)
      return 1;
    return -1;
  }

  struct PriorityVal {
    int priority;
    int size;
  };

  static int compare_priority_val(const PriorityVal *l, const PriorityVal *r) {
    // Subtracting the priorities is unsafe, but it's fine for this test.
    int priority_diff = l->priority - r->priority;
    if (priority_diff != 0) {
      return priority_diff;
    }
    if (l->size == r->size)
      return 0;
    if (l->size > r->size)
      return 1;
    return -1;
  }

  // The following test is intended to mimic the CPP library pattern of having a
  // comparison function that takes a specific type, which is passed to a
  // library that then needs to sort an array of that type. The library can't
  // safely pass the comparison function to qsort because a function that takes
  // const T* being cast to a function that takes const void* is undefined
  // behavior. The safer pattern is to pass a type erased comparator that calls
  // into the typed comparator to qsort_r.
  template <typename T>
  static int type_erased_comp(const void *l, const void *r,
                              void *erased_func_ptr) {
    using TypedComp = int (*)(const T *, const T *);
    TypedComp typed_func_ptr = *static_cast<TypedComp *>(erased_func_ptr);
    const T *lt = static_cast<const T *>(l);
    const T *rt = static_cast<const T *>(r);
    return typed_func_ptr(lt, rt);
  }

public:
  void sorted_array(QsortFnTy func) {
    int array[25] = {10,   23,   33,   35,   55,   70,    71,   100,  110,
                     123,  133,  135,  155,  170,  171,   1100, 1110, 1123,
                     1133, 1135, 1155, 1170, 1171, 11100, 12310};
    constexpr SizeTy ARRAY_SIZE = sizeof(array) / sizeof(int);
    int expected[ARRAY_SIZE];
    for (SizeTy i = 0; i < ARRAY_SIZE; ++i)
      expected[i] = array[i];
    SizeTy count = 0;

    func(array, ARRAY_SIZE, sizeof(int), int_compare_count, &count);

    for (SizeTy i = 0; i < ARRAY_SIZE; ++i)
      ASSERT_EQ(array[i], expected[i]);

    // This is a sorted list, but there still have to have been at least N - 1
    // comparisons made.
    ASSERT_GE(count, ARRAY_SIZE - 1);
  }

  void reverse_sorted_array(QsortFnTy func) {
    int array[25] = {25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13,
                     12, 11, 10, 9,  8,  7,  6,  5,  4,  3,  2,  1};
    constexpr SizeTy ARRAY_SIZE = sizeof(array) / sizeof(int);

    SizeTy count = 0;

    func(array, ARRAY_SIZE, sizeof(int), int_compare_count, &count);

    for (int i = 0; i < int(ARRAY_SIZE); ++i)
      ASSERT_EQ(array[i], i + 1);

    ASSERT_GE(count, ARRAY_SIZE);
  }

  void safe_type_erasure(QsortFnTy func) {
    PriorityVal array[5] = {
        {10, 3}, {1, 10}, {-1, 100}, {10, 0}, {3, 3},
    };
    constexpr SizeTy ARRAY_SIZE = sizeof(array) / sizeof(PriorityVal);

    auto compare = compare_priority_val;
    func(array, ARRAY_SIZE, sizeof(PriorityVal), type_erased_comp<PriorityVal>,
         &compare);

    EXPECT_EQ(array[0].priority, -1);
    EXPECT_EQ(array[0].size, 100);
    EXPECT_EQ(array[1].priority, 1);
    EXPECT_EQ(array[1].size, 10);
    EXPECT_EQ(array[2].priority, 3);
    EXPECT_EQ(array[2].size, 3);
    EXPECT_EQ(array[3].priority, 10);
    EXPECT_EQ(array[3].size, 0);
    EXPECT_EQ(array[4].priority, 10);
    EXPECT_EQ(array[4].size, 3);
  }

  void empty_array(QsortFnTy func) {
    int value = 42;
    SizeTy count = 0;
    func(&value, 0, sizeof(value), int_compare_count, &count);
    EXPECT_EQ(count, SizeTy(0));
    EXPECT_EQ(value, 42);
  }

  void duplicate_keys(QsortFnTy func) {
    int array[] = {2, -1, 2, 0, -1, 2, 0};
    const int expected[] = {-1, -1, 0, 0, 2, 2, 2};
    constexpr SizeTy ARRAY_SIZE = sizeof(array) / sizeof(int);
    SizeTy count = 0;
    func(array, ARRAY_SIZE, sizeof(int), int_compare_count, &count);
    for (SizeTy i = 0; i < ARRAY_SIZE; ++i)
      ASSERT_EQ(array[i], expected[i]);
  }
};

#define QSORTREENTRANT_TEST(name, func, sizetype)                              \
  using LlvmLibc##name##Test = QsortReentrantTest<decltype(func), sizetype>;   \
  TEST_F(LlvmLibc##name##Test, SortedArray) { sorted_array(func); }            \
  TEST_F(LlvmLibc##name##Test, ReverseSortedArray) {                           \
    reverse_sorted_array(func);                                                \
  }                                                                            \
  TEST_F(LlvmLibc##name##Test, SafeTypeErasure) { safe_type_erasure(func); }      \
  TEST_F(LlvmLibc##name##Test, EmptyArray) { empty_array(func); }                \
  TEST_F(LlvmLibc##name##Test, DuplicateKeys) { duplicate_keys(func); }
