//===-- Numbered message catalog validation tests -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/nl_types/catalog_data.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::internal::MessageCatalog;
namespace {
struct Fixture {
  // Deliberately unaligned file origin.
  uint8_t allocation[73]{};
  uint8_t *bytes = allocation + 1;
  static void put(uint8_t *p, uint32_t value, bool big) {
    for (unsigned i = 0; i != 4; ++i)
      p[big ? 3 - i : i] = uint8_t(value >> (8 * i));
  }
  void cell(unsigned index, uint32_t set, uint32_t message, uint32_t offset) {
    uint32_t values[] = {set, message, offset};
    for (unsigned i = 0; i != 3; ++i) {
      put(bytes + 12 + index * 12 + i * 4, values[i], false);
      put(bytes + 36 + index * 12 + i * 4, values[i], true);
    }
  }
  Fixture(bool big = false) {
    put(bytes, 0x960408de, big);
    put(bytes + 4, 1, big);
    put(bytes + 8, 2, big);
    cell(0, 2, 1, 0);
    cell(1, 3, 2, 6);
    const char text[] = "hello\0world";
    for (unsigned i = 0; i != sizeof(text); ++i)
      bytes[60 + i] = text[i];
  }
  auto load() { return MessageCatalog::copy({bytes, 72}); }
};
} // namespace

TEST(LlvmLibcCatalogDataTest, ByteOrdersUnalignedAndOwned) {
  for (unsigned order = 0; order != 2; ++order) {
    Fixture f(order);
    auto catalog = f.load();
    ASSERT_TRUE(catalog.has_value());
    f.bytes[60] = 'X';
    EXPECT_STREQ((*catalog)->lookup(1, 1), "hello");
    EXPECT_STREQ((*catalog)->lookup(2, 2), "world");
    EXPECT_EQ((*catalog)->lookup(2, 1), nullptr);
    EXPECT_EQ((*catalog)->lookup(-1, 1), nullptr);
    EXPECT_EQ((*catalog)->lookup(1, -1), nullptr);
    EXPECT_EQ((*catalog)->lookup(0x7fffffff, 0x7fffffff), nullptr);
    (*catalog)->destroy();
  }
}

TEST(LlvmLibcCatalogDataTest, TruncationAndDimensions) {
  Fixture f;
  for (unsigned length = 0; length != 72; ++length)
    EXPECT_FALSE(MessageCatalog::copy({f.bytes, length}).has_value());
  Fixture::put(f.bytes + 4, 0, false);
  EXPECT_EQ(f.load().error(), EINVAL);
  Fixture::put(f.bytes + 4, ~0U, false);
  Fixture::put(f.bytes + 8, ~0U, false);
  EXPECT_EQ(f.load().error(), EINVAL);
  EXPECT_EQ(MessageCatalog::copy({nullptr, size_t(0)}).error(), EINVAL);
}

TEST(LlvmLibcCatalogDataTest, IndicesAndStringBounds) {
  {
    Fixture f;
    f.bytes[36] ^= 1;
    EXPECT_EQ(f.load().error(), EINVAL);
  }
  {
    Fixture f;
    f.cell(1, 2, 1, 6);
    EXPECT_EQ(f.load().error(), EINVAL);
  }
  {
    Fixture f;
    f.cell(1, 0, 1, 0);
    EXPECT_EQ(f.load().error(), EINVAL);
  }
  {
    Fixture f;
    f.cell(1, 3, 2, 12);
    EXPECT_EQ(f.load().error(), EINVAL);
  }
  {
    Fixture f;
    f.bytes[71] = 'X';
    EXPECT_EQ(f.load().error(), EINVAL);
  }
  {
    Fixture f;
    f.cell(1, 3, 0xffffffff, 6);
    EXPECT_EQ(f.load().error(), EINVAL);
  }
}

TEST(LlvmLibcCatalogDataTest, EmptyCatalogAndSuffixes) {
  uint8_t empty[36]{};
  Fixture::put(empty, 0x960408de, true);
  Fixture::put(empty + 4, 1, true);
  Fixture::put(empty + 8, 1, true);
  auto catalog = MessageCatalog::copy(empty);
  ASSERT_TRUE(catalog.has_value());
  EXPECT_EQ((*catalog)->lookup(1, 1), nullptr);
  (*catalog)->destroy();
  Fixture f;
  f.cell(1, 3, 2, 3);
  catalog = f.load();
  ASSERT_TRUE(catalog.has_value());
  EXPECT_STREQ((*catalog)->lookup(2, 2), "lo");
  (*catalog)->destroy();
}

#ifdef CATALOG_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned calls = 0, fail_call = 0, live = 0;
void *allocations[8]{};
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++calls == fail_call)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p)
    for (void *&slot : allocations)
      if (!slot) {
        slot = p;
        ++live;
        break;
      }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (p)
    for (void *&slot : allocations)
      if (slot == p) {
        slot = nullptr;
        --live;
        break;
      }
  __real_free(p);
}
TEST(LlvmLibcCatalogDataTest, AllocationAndMalformedInputRelease) {
  for (unsigned failure = 1; failure != 4; ++failure) {
    Fixture f;
    calls = 0;
    fail_call = failure;
    tracking = true;
    auto catalog = f.load();
    tracking = false;
    EXPECT_FALSE(catalog.has_value());
    EXPECT_EQ(catalog.error(), ENOMEM);
    EXPECT_EQ(live, 0U);
  }
  Fixture f;
  calls = fail_call = 0;
  tracking = true;
  auto catalog = f.load();
  tracking = false;
  ASSERT_TRUE(catalog.has_value());
  EXPECT_EQ(live, 3U);
  (*catalog)->destroy();
  EXPECT_EQ(live, 0U);
  f.cell(1, 2, 1, 6);
  tracking = true;
  catalog = f.load();
  tracking = false;
  EXPECT_EQ(catalog.error(), EINVAL);
  EXPECT_EQ(live, 0U);
}
#endif
