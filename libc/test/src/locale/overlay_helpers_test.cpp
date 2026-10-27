//===-- Locale helpers without private full-build handles -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/locale/collation.h"
#include "src/nl_types/catalog_path.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE::internal;

TEST(LlvmLibcOverlayHelpersTest, BuiltinCollationNeedsNoPrivateLocaleHandle) {
  auto *category = current_collation();
  EXPECT_EQ(category, static_cast<const __locale_data *>(nullptr));
  EXPECT_LT(collate("a", "b", category), 0);
  EXPECT_EQ(collate("same", "same", category), 0);
  EXPECT_GT(collate(L"b", L"a", category), 0);
  char result[4];
  EXPECT_EQ(collate_transform(result, "abc", sizeof(result), category),
            size_t(3));
  EXPECT_STREQ(result, "abc");
}

TEST(LlvmLibcOverlayHelpersTest, MessagePathNeedsNoPublicCatalogHandle) {
  char result[64];
  auto size = expand_catalog_path("%l/%t/%c/%N", "app", "de_DE.UTF-8",
                                  {result, sizeof(result)});
  ASSERT_TRUE(size.has_value());
  EXPECT_STREQ(result, "de/DE/UTF-8/app");
}
