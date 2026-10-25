//===-- Encoding descriptor ownership tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/wchar/encoding_descriptor.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::cpp::span;
using LIBC_NAMESPACE::internal::EncodingDescriptor;
using LIBC_NAMESPACE::internal::LocaleDataFile;

namespace {
struct Fixture {
  uint8_t storage[275]{};
  uint8_t *bytes = storage + 1;
  static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i != 4; ++i)
      p[i] = uint8_t(value >> (24 - 8 * i));
  }
  Fixture() {
    const char magic[] = "LLVMLCL";
    for (unsigned i = 0; i != 8; ++i)
      bytes[i] = uint8_t(magic[i]);
    put(bytes + 8, 1);
    put(bytes + 12, 0x01020304);
    put(bytes + 16, 274);
    put(bytes + 20, 6);
    put(bytes + 24, 128);
    put(bytes + 28, 2);
    bytes[128] = 'x';
    for (unsigned i = 0; i != 6; ++i) {
      uint8_t *directory = bytes + 32 + i * 16;
      put(directory, i);
      put(directory + 4, i ? 274 : 130);
      put(directory + 8, i ? 0 : 144);
      put(directory + 12, i ? 0 : 6);
      uint8_t *entry = bytes + 130 + i * 16;
      put(entry, 0x201 + i);
      put(entry + 4, 2);
      put(entry + 8, 96 + i * 8);
      put(entry + 12, 8);
      uint8_t *pair = bytes + 226 + i * 8;
      put(pair, i % 2 ? 0x4e2d : 0x2121);
      put(pair + 4, i % 2 ? 0x2121 : 0x4e2d);
    }
  }
  auto copy() { return LocaleDataFile::copy({bytes, 274}); }
};
} // namespace

TEST(LlvmLibcEncodingDescriptorTest, StatelessAndInvalidNames) {
  auto created = EncodingDescriptor::create("UTF-8", "UTF-16BE");
  ASSERT_TRUE(created.has_value());
  uint8_t result[8]{};
  const uint8_t bytes[] = {'A', 0xc3, 0xa4};
  span<const uint8_t> input(bytes);
  span<uint8_t> output(result);
  auto converted = (*created)->convert(input, output);
  EXPECT_TRUE(converted.has_value());
  EXPECT_TRUE(input.empty());
  EXPECT_EQ(output.size(), size_t(4));
  EXPECT_EQ(result[0], uint8_t(0));
  EXPECT_EQ(result[1], uint8_t('A'));
  EXPECT_EQ(result[2], uint8_t(0));
  EXPECT_EQ(result[3], uint8_t(0xe4));
  EXPECT_TRUE((*created)->reset(nullptr).has_value());
  (*created)->destroy();
  EXPECT_EQ(EncodingDescriptor::create("UTF-16", "UTF-8").error(), EINVAL);
  EXPECT_EQ(EncodingDescriptor::create("UTF-8", "ASCII//unknown").error(),
            EINVAL);
  EXPECT_EQ(EncodingDescriptor::create("ISO-2022-CN", "UTF-8").error(), EINVAL);
}

TEST(LlvmLibcEncodingDescriptorTest, RetainedTablesAndIndependentState) {
  Fixture f;
  auto file = f.copy();
  ASSERT_TRUE(file.has_value());
  auto first = EncodingDescriptor::create("ISO-2022-CN", "UTF-8", *file);
  auto second = EncodingDescriptor::create("ISO-2022-CN", "UTF-8", *file);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  (*file)->release();
  for (auto &byte : f.storage)
    byte = 0xff;
  const uint8_t designation[] = {0x1b, '$', ')', 'A'};
  span<const uint8_t> input(designation);
  uint8_t result[8]{};
  span<uint8_t> output(result);
  EXPECT_TRUE((*first)->convert(input, output).has_value());
  EXPECT_TRUE(input.empty());
  EXPECT_EQ(output.size(), size_t(8));
  const uint8_t character[] = {0x0e, '!', '!'};
  input = span<const uint8_t>(character);
  EXPECT_EQ((*second)->convert(input, output).error(), EILSEQ);
  EXPECT_EQ(input.size(), sizeof(character));
  EXPECT_TRUE((*first)->convert(input, output).has_value());
  EXPECT_TRUE(input.empty());
  EXPECT_EQ(result[0], uint8_t(0xe4));
  EXPECT_EQ(result[1], uint8_t(0xb8));
  EXPECT_EQ(result[2], uint8_t(0xad));
  (*first)->destroy();
  const uint8_t whole[] = {0x1b, '$', ')', 'A', 0x0e, '!', '!'};
  input = span<const uint8_t>(whole);
  output = span<uint8_t>(result);
  EXPECT_TRUE((*second)->convert(input, output).has_value());
  EXPECT_TRUE(input.empty());
  (*second)->destroy();
}

TEST(LlvmLibcEncodingDescriptorTest, RejectMalformedMappingBeforePublication) {
  Fixture f;
  Fixture::put(f.bytes + 226, 0x2021);
  auto file = f.copy();
  ASSERT_TRUE(file.has_value());
  auto invalid = EncodingDescriptor::create("ISO-2022-CN", "UTF-8", *file);
  EXPECT_FALSE(invalid.has_value());
  // Encodings not using the table do not depend on its contents or lifetime.
  auto ordinary = EncodingDescriptor::create("ASCII", "UTF-8", *file);
  EXPECT_TRUE(ordinary.has_value());
  (*file)->release();
  if (ordinary)
    (*ordinary)->destroy();
}

#ifdef ENCODING_DESCRIPTOR_ALLOCATION_TEST
namespace {
bool tracking = false, fail_allocation = false;
void *allocations[8]{};
int live = 0;
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (fail_allocation)
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

TEST(LlvmLibcEncodingDescriptorTest, AllocationFailureAndRecovery) {
  Fixture f;
  auto file = f.copy();
  ASSERT_TRUE(file.has_value());
  fail_allocation = true;
  auto failure = EncodingDescriptor::create("UTF-8", "ISO-2022-CN", *file);
  fail_allocation = false;
  ASSERT_FALSE(failure.has_value());
  EXPECT_EQ(failure.error(), ENOMEM);
  auto recovered = EncodingDescriptor::create("UTF-8", "ISO-2022-CN", *file);
  EXPECT_TRUE(recovered.has_value());
  (*file)->release();
  if (recovered)
    (*recovered)->destroy();
}

TEST(LlvmLibcEncodingDescriptorTest, LastDescriptorReleasesFile) {
  Fixture f;
  tracking = true;
  auto file = f.copy();
  tracking = false;
  ASSERT_TRUE(file.has_value());
  EXPECT_EQ(live, 2);
  tracking = true;
  auto first = EncodingDescriptor::create("UTF-8", "ISO-2022-CN", *file);
  auto second = EncodingDescriptor::create("UTF-8", "ISO-2022-CN", *file);
  tracking = false;
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(live, 4);
  (*file)->release();
  EXPECT_EQ(live, 4);
  (*first)->destroy();
  EXPECT_EQ(live, 3);
  (*second)->destroy();
  EXPECT_EQ(live, 0);
}
#endif
