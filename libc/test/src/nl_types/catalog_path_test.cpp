//===-- Numbered catalog search tests -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/nl_types/catalog_path.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;
using namespace LIBC_NAMESPACE::internal;

TEST(LlvmLibcCatalogPathTest, SubstitutionsAndBounds) {
  char out[128];
  auto result = expand_catalog_path("%N/%L/%l/%t/%c/%%", "app",
                                    "de_DE.UTF-8@euro", {out, sizeof(out)});
  ASSERT_TRUE(result.has_value());
  EXPECT_STREQ(out, "app/de_DE.UTF-8@euro/de/DE/UTF-8/%");
  ASSERT_TRUE(expand_catalog_path("%l:%t:%c", "app", "C", {out, sizeof(out)})
                  .has_value());
  EXPECT_STREQ(out, "C::");
  ASSERT_TRUE(expand_catalog_path("", "app", "C", {out, 4}).has_value());
  EXPECT_STREQ(out, "app");
  EXPECT_EQ(expand_catalog_path("", "app", "C", {out, 3}).error(),
            ENAMETOOLONG);
  EXPECT_EQ(expand_catalog_path("%N", "app", "C", {out, size_t(0)}).error(),
            ENAMETOOLONG);
  EXPECT_EQ(expand_catalog_path("%", "app", "C", {out, sizeof(out)}).error(),
            EINVAL);
  EXPECT_EQ(expand_catalog_path("%Q", "app", "C", {out, sizeof(out)}).error(),
            EINVAL);
}

TEST(LlvmLibcCatalogPathTest, SearchOrderAndInvalidCatalogFallback) {
  unsigned count = 0;
  const char *expected[] = {"missing/app", "bad/app", "app", "C/app", "app"};
  auto result =
      search_catalog("app", "C", "missing/%N:bad/%N:", "%L/%N:%N",
                     [&](cpp::string_view path) -> ErrorOr<MessageCatalog *> {
                       EXPECT_LT(count, 5U);
                       if (count < 5)
                         EXPECT_TRUE(path == expected[count]);
                       return Error(++count == 2 ? EINVAL : ENOENT);
                     });
  EXPECT_EQ(result.error(), ENOENT);
  EXPECT_EQ(count, 5U);
  count = 0;
  auto literal =
      search_catalog("./app", "C", "%N", "%N",
                     [&](cpp::string_view path) -> ErrorOr<MessageCatalog *> {
                       EXPECT_TRUE(path == "./app");
                       ++count;
                       return Error(EACCES);
                     });
  EXPECT_EQ(literal.error(), EACCES);
  EXPECT_EQ(count, 1U);
}

TEST(LlvmLibcCatalogPathTest, ResourceErrorsStopSearch) {
  const int errors[] = {ENOMEM, EMFILE, ENFILE, EINTR};
  for (int error : errors) {
    unsigned count = 0;
    auto result =
        search_catalog("app", "C", "%N:%N", "%N",
                       [&](cpp::string_view) -> ErrorOr<MessageCatalog *> {
                         ++count;
                         return Error(error);
                       });
    EXPECT_EQ(result.error(), error);
    EXPECT_EQ(count, 1U);
  }
}
