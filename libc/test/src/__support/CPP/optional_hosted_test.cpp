//===-- Hosted optional tests ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifdef HOST_NEW_BEFORE
#include <new>
#endif
#include "src/__support/CPP/optional.h"
#ifdef HOST_NEW_AFTER
#include <new>
#endif
#include "test/UnitTest/Test.h"

namespace {
struct alignas(256) Value {
  int &live;
  int value;
  Value(int &live, int value) : live(live), value(value) { ++live; }
  Value(const Value &other) : live(other.live), value(other.value) { ++live; }
  Value &operator=(const Value &other) {
    value = other.value;
    return *this;
  }
  ~Value() { --live; }
};
} // namespace

TEST(LlvmLibcOptionalHostedTest, NontrivialLifetime) {
  int live = 0;
  {
    Value source(live, 7);
    LIBC_NAMESPACE::cpp::optional<Value> value;
    value = source;
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->value, 7);
    EXPECT_EQ(live, 2);
    EXPECT_EQ(reinterpret_cast<size_t>(&*value) % alignof(Value), size_t(0));
    value = source;
    EXPECT_EQ(live, 2);
    value.reset();
    EXPECT_EQ(live, 1);
    value = source;
    EXPECT_EQ(live, 2);
  }
  EXPECT_EQ(live, 0);
}

TEST(LlvmLibcOptionalHostedTest, HostPlacementNewIsUnchanged) {
  int live = 0;
  alignas(Value) unsigned char storage[sizeof(Value)];
  Value *value = ::new (static_cast<void *>(storage)) Value(live, 11);
  EXPECT_EQ(value->value, 11);
  EXPECT_EQ(live, 1);
  value->~Value();
  EXPECT_EQ(live, 0);
}
