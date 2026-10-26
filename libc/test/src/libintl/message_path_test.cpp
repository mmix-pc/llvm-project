//===-- Translation catalog search tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/libintl/message_path.h"
#include "test/UnitTest/Test.h"

using namespace LIBC_NAMESPACE;
using internal::message_object_path;
using internal::MoCatalog;
using internal::search_message_object;

TEST(LlvmLibcMessagePathTest, ConstructionAndBounds) {
  char out[128];
  auto result = message_object_path("/catalogs///", "de_DE.UTF-8",
                                    "LC_MESSAGES", "mail", {out, sizeof(out)});
  ASSERT_TRUE(result.has_value());
  EXPECT_STREQ(out, "/catalogs/de_DE.UTF-8/LC_MESSAGES/mail.mo");
  result = message_object_path("/", "tr_TR", "LC_TIME", "calendar",
                               {out, sizeof(out)});
  ASSERT_TRUE(result.has_value());
  EXPECT_STREQ(out, "/tr_TR/LC_TIME/calendar.mo");
  result = message_object_path("relative", "C.UTF-8", "LC_MESSAGES", "messages",
                               {out, sizeof(out)});
  ASSERT_TRUE(result.has_value());
  EXPECT_STREQ(out, "relative/C.UTF-8/LC_MESSAGES/messages.mo");
  for (char &c : out)
    c = '@';
  EXPECT_EQ(message_object_path("/d", "l", "c", "n", {out, 5}).error(),
            ENAMETOOLONG);
  EXPECT_EQ(out[5], '@');
}

TEST(LlvmLibcMessagePathTest, NLSPATHPrecedesLanguageAndLocale) {
  const char *expected[] = {"/absent/mail", "/nl/de_DE.UTF-8/mail"};
  unsigned calls = 0;
  auto open = [&](cpp::string_view path) -> ErrorOr<MoCatalog *> {
    EXPECT_LT(calls, 2U);
    if (calls >= 2)
      return Error(EINVAL);
    EXPECT_TRUE(path == expected[calls]);
    return ++calls == 2
               ? ErrorOr<MoCatalog *>(static_cast<MoCatalog *>(nullptr))
               : ErrorOr<MoCatalog *>(Error(ENOENT));
  };
  auto result =
      search_message_object("/catalogs", "de_DE.UTF-8", "LC_MESSAGES", "mail",
                            "/absent/%N:/nl/%L/%N", "tr_TR", open);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(calls, 2U);
}

TEST(LlvmLibcMessagePathTest, LanguageOrderAndSingleLocaleFallback) {
  const char *expected[] = {"/catalogs/de_DE/LC_MESSAGES/mail.mo",
                            "/catalogs/tr_TR/LC_MESSAGES/mail.mo",
                            "/catalogs/en_US.UTF-8/LC_MESSAGES/mail.mo"};
  unsigned calls = 0;
  auto open = [&](cpp::string_view path) -> ErrorOr<MoCatalog *> {
    EXPECT_LT(calls, 3U);
    if (calls >= 3)
      return Error(EINVAL);
    EXPECT_TRUE(path == expected[calls++]);
    return Error(ENOENT);
  };
  auto result =
      search_message_object("/catalogs", "en_US.UTF-8", "LC_MESSAGES", "mail",
                            nullptr, "de_DE:../bad:.:/abs::tr_TR", open);
  EXPECT_EQ(result.error(), ENOENT);
  EXPECT_EQ(calls, 3U);
}

TEST(LlvmLibcMessagePathTest, ResourceFailureStopsSearch) {
  unsigned calls = 0;
  auto open = [&](cpp::string_view) -> ErrorOr<MoCatalog *> {
    ++calls;
    return Error(ENOMEM);
  };
  auto result = search_message_object("/catalogs", "de_DE", "LC_MESSAGES",
                                      "mail", "/first:/second", "tr_TR", open);
  EXPECT_EQ(result.error(), ENOMEM);
  EXPECT_EQ(calls, 1U);
  result = search_message_object("/catalogs", "C", "LC_MESSAGES", "mail",
                                 "/first", "tr_TR", open);
  EXPECT_EQ(result.error(), ENOENT);
  EXPECT_EQ(calls, 1U);
}

TEST(LlvmLibcMessagePathTest,
     InvalidTemplateAndObjectDoNotMaskLaterCandidates) {
  const char *expected[] = {"mail", "/valid/mail"};
  unsigned calls = 0;
  auto open = [&](cpp::string_view path) -> ErrorOr<MoCatalog *> {
    EXPECT_LT(calls, 2U);
    if (calls >= 2)
      return Error(EINVAL);
    EXPECT_TRUE(path == expected[calls]);
    return ++calls == 2
               ? ErrorOr<MoCatalog *>(static_cast<MoCatalog *>(nullptr))
               : ErrorOr<MoCatalog *>(Error(EINVAL));
  };
  auto result = search_message_object("/catalogs", "de_DE", "LC_MESSAGES",
                                      "mail", "%Q::/valid/%N", nullptr, open);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(calls, 2U);
}
