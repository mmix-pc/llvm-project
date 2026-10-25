//===-- Unittests for Optional --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/optional.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::nullopt;
using LIBC_NAMESPACE::cpp::optional;

// This class has three properties for testing:
// 1) No default constructor.
// 2) A non-trivial destructor with an observable side-effect.
// 3) Functions that can be called explicitly.
class Contrived {
  int *_a;

public:
  Contrived(int *a) : _a(a) {}
  ~Contrived() { (*_a)++; }

  int get_a() { return *_a; }
  void inc_a() { (*_a)++; }
};

TEST(LlvmLibcOptionalTest, Tests) {
  optional<int> Trivial1(12);
  ASSERT_TRUE(Trivial1.has_value());
  ASSERT_EQ(Trivial1.value(), 12);
  ASSERT_EQ(*Trivial1, 12);
  Trivial1.reset();
  ASSERT_FALSE(Trivial1.has_value());

  optional<int> Trivial2(12);
  ASSERT_TRUE(Trivial2.has_value());
  Trivial2 = nullopt;
  ASSERT_FALSE(Trivial2.has_value());

  // For this test case, the destructor increments the pointed-to value.
  int holding = 1;
  {
    optional<Contrived> Complicated(&holding);
    // Destructor was run once as part of copying the object.
    ASSERT_EQ(holding, 2);
    // Destructor was run a second time as part of destruction.
    Complicated.reset();
    ASSERT_EQ(holding, 3);
    // Destructor was not run a third time as the object is already destroyed.
    Complicated.reset();
    ASSERT_EQ(holding, 3);
  }
  // Make sure the destructor isn't called when the optional is destroyed.
  ASSERT_EQ(holding, 3);

  // Test that assigning an optional to another works when set
  optional<int> Trivial3(12);
  optional<int> Trivial4 = Trivial3;
  ASSERT_TRUE(Trivial4.has_value());
  ASSERT_EQ(Trivial4.value(), 12);

  // Test that assigning an option to another works when unset
  optional<int> Trivial5;
  ASSERT_FALSE(Trivial5.has_value());
  optional<int> Trivial6 = Trivial5;
  ASSERT_FALSE(Trivial6.has_value());

  // Test operator->
  int arrow_num = 5;
  optional<Contrived> arrow_test(&arrow_num);
  ASSERT_TRUE(arrow_test.has_value());
  ASSERT_EQ(arrow_test->get_a(), arrow_num);
  arrow_num = 10;
  ASSERT_EQ(arrow_test->get_a(), arrow_num);
  arrow_test->inc_a();
  ASSERT_EQ(arrow_test->get_a(), arrow_num);
  ASSERT_EQ(arrow_num, 11);
  arrow_test.reset();
}

TEST(LlvmLibcOptionalTest, ValueOr) {
  optional<int> opt_empty;
  EXPECT_EQ(opt_empty.value_or(42), 42);
  optional<int> opt_full(10);
  EXPECT_EQ(opt_full.value_or(42), 10);
  EXPECT_EQ(optional<int>(100).value_or(42), 100);
  EXPECT_EQ(optional<int>().value_or(42), 42);

  const optional<int> opt_const_empty;
  EXPECT_EQ(opt_const_empty.value_or(42), 42);
  const optional<int> opt_const_full(10);
  EXPECT_EQ(opt_const_full.value_or(42), 10);

  optional<long> opt_long_empty;
  EXPECT_EQ(opt_long_empty.value_or(42), 42L);
  optional<long> opt_long_full(100L);
  EXPECT_EQ(opt_long_full.value_or(42), 100L);
}

namespace {
constexpr bool trivial_assignment() {
  optional<int> value;
  value = 7;
  if (!value || *value != 7)
    return false;
  const int next = 11;
  value = next;
  value.reset();
  value = next;
  return value && *value == 11;
}
static_assert(trivial_assignment());

struct Counts {
  int copies = 0, moves = 0, assignments = 0, destroyed = 0;
};
struct Tracked {
  Counts &counts;
  int value;
  Tracked(Counts &counts, int value) : counts(counts), value(value) {}
  Tracked(const Tracked &other) : counts(other.counts), value(other.value) {
    ++counts.copies;
  }
  Tracked(Tracked &&other) : counts(other.counts), value(other.value) {
    ++counts.moves;
    other.value = -1;
  }
  Tracked &operator=(const Tracked &other) {
    ++counts.assignments;
    value = other.value;
    return *this;
  }
  Tracked &operator=(Tracked &&other) {
    ++counts.assignments;
    value = other.value;
    other.value = -1;
    return *this;
  }
  ~Tracked() { ++counts.destroyed; }
};
struct MoveOnly {
  int value;
  explicit MoveOnly(int value) : value(value) {}
  MoveOnly(const MoveOnly &) = delete;
  MoveOnly &operator=(const MoveOnly &) = delete;
  MoveOnly(MoveOnly &&other) : value(other.value) { other.value = -1; }
  MoveOnly &operator=(MoveOnly &&other) {
    value = other.value;
    other.value = -1;
    return *this;
  }
};
} // namespace

TEST(LlvmLibcOptionalTest, ValueAssignmentLifetime) {
  Counts counts;
  {
    const Tracked source(counts, 3);
    optional<Tracked> value;
    value = source;
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(value->value, 3);
    EXPECT_EQ(counts.copies, 1);
    EXPECT_EQ(counts.destroyed, 0);
    value = source;
    EXPECT_EQ(counts.assignments, 1);
    EXPECT_EQ(counts.copies, 1);
    value = *value;
    EXPECT_EQ(value->value, 3);
    value.reset();
    EXPECT_EQ(counts.destroyed, 1);
    Tracked moved(counts, 7);
    value = LIBC_NAMESPACE::cpp::move(moved);
    EXPECT_EQ(value->value, 7);
    EXPECT_EQ(moved.value, -1);
    EXPECT_EQ(counts.moves, 1);
    EXPECT_EQ(counts.destroyed, 1);
    Tracked replacement(counts, 13);
    value = LIBC_NAMESPACE::cpp::move(replacement);
    EXPECT_EQ(value->value, 13);
    EXPECT_EQ(replacement.value, -1);
    EXPECT_EQ(counts.assignments, 3);
    EXPECT_EQ(counts.moves, 1);
  }
  EXPECT_EQ(counts.destroyed, 5);
}

TEST(LlvmLibcOptionalTest, MoveOnlyValueAssignment) {
  optional<MoveOnly> value;
  value = MoveOnly(7);
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(value->value, 7);
  value = MoveOnly(9);
  EXPECT_EQ(value->value, 9);
  value.reset();
  value = MoveOnly(11);
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(value->value, 11);
}
