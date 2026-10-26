//===-- Translation domain binding tests ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/errno_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/libc_errno.h"
#include "src/libintl/bind_textdomain_codeset.h"
#include "src/libintl/bindtextdomain.h"
#include "src/libintl/domain_bindings.h"
#include "src/libintl/textdomain.h"
#include "test/UnitTest/Test.h"
#ifdef DOMAIN_BINDINGS_THREADS
#include "src/pthread/pthread_create.h"
#include "src/pthread/pthread_join.h"
#endif

TEST(LlvmLibcDomainBindingsTest, OwnedLookupSnapshot) {
  using namespace LIBC_NAMESPACE::internal;
  ASSERT_TRUE(current_text_domain("snapshot-a").has_value());
  ASSERT_TRUE(
      bind_text_domain("snapshot-a", "/before", DomainBinding::DIRECTORY)
          .has_value());
  ASSERT_TRUE(bind_text_domain("snapshot-a", "UTF-8", DomainBinding::CODESET)
                  .has_value());
  DomainSnapshot snapshot;
  ASSERT_TRUE(snapshot_text_domain(nullptr, snapshot).has_value());
  ASSERT_TRUE(current_text_domain("snapshot-b").has_value());
  ASSERT_TRUE(bind_text_domain("snapshot-a", "/after", DomainBinding::DIRECTORY)
                  .has_value());
  ASSERT_TRUE(bind_text_domain("snapshot-a", "ASCII", DomainBinding::CODESET)
                  .has_value());
  EXPECT_STREQ(snapshot.domain(), "snapshot-a");
  EXPECT_STREQ(snapshot.directory(), "/before");
  EXPECT_STREQ(snapshot.codeset(), "UTF-8");
  DomainSnapshot explicit_domain;
  ASSERT_TRUE(snapshot_text_domain("snapshot-a", explicit_domain).has_value());
  EXPECT_STREQ(explicit_domain.directory(), "/after");
  EXPECT_STREQ(explicit_domain.codeset(), "ASCII");
  DomainSnapshot defaults;
  ASSERT_TRUE(snapshot_text_domain("", defaults).has_value());
  EXPECT_STREQ(defaults.domain(), "messages");
  EXPECT_STREQ(defaults.directory(), "/usr/share/locale");
  EXPECT_EQ(defaults.codeset(), nullptr);
  EXPECT_TRUE(current_text_domain("").has_value());
}

using LIBC_NAMESPACE::bind_textdomain_codeset;
using LIBC_NAMESPACE::bindtextdomain;
using LIBC_NAMESPACE::libc_errno;
using LIBC_NAMESPACE::textdomain;

TEST(LlvmLibcDomainBindingsTest, CurrentDomainCopiesQueriesAndResets) {
  EXPECT_STREQ(textdomain(""), "messages");
  char name[] = "application";
  ASSERT_NE(textdomain(name), nullptr);
  name[0] = 'X';
  libc_errno = EDOM;
  char *saved = textdomain(nullptr);
  EXPECT_STREQ(saved, "application");
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(textdomain(saved), saved);
  EXPECT_STREQ(textdomain(saved + 1), "pplication");
  EXPECT_STREQ(textdomain(""), "messages");
}

TEST(LlvmLibcDomainBindingsTest, QueriesDoNotCreateOrFreezeBindings) {
  libc_errno = EDOM;
  EXPECT_EQ(bindtextdomain(nullptr, "dir"), nullptr);
  EXPECT_EQ(bindtextdomain("", "dir"), nullptr);
  EXPECT_EQ(bind_textdomain_codeset(nullptr, "UTF-8"), nullptr);
  EXPECT_EQ(bind_textdomain_codeset("", "UTF-8"), nullptr);
  EXPECT_EQ(bind_textdomain_codeset("never-bound", nullptr), nullptr);
  EXPECT_STREQ(bindtextdomain("never-bound", nullptr), "/usr/share/locale");
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_STREQ(bindtextdomain("directory-only", "relative"), "relative");
  EXPECT_EQ(bind_textdomain_codeset("directory-only", nullptr), nullptr);
  EXPECT_EQ(int(libc_errno), EDOM);
}

TEST(LlvmLibcDomainBindingsTest, FieldsAndDomainsHaveIndependentLifetimes) {
  char domain[] = "owned-domain", directory[] = "one", codeset[] = "UTF-8";
  char *dir = bindtextdomain(domain, directory);
  ASSERT_NE(dir, nullptr);
  char *code = bind_textdomain_codeset(domain, codeset);
  ASSERT_NE(code, nullptr);
  domain[0] = directory[0] = codeset[0] = 'X';
  EXPECT_STREQ(dir, "one");
  EXPECT_STREQ(code, "UTF-8");
  EXPECT_EQ(bindtextdomain("owned-domain", dir), dir);
  EXPECT_EQ(bind_textdomain_codeset("owned-domain", code), code);
  ASSERT_NE(bindtextdomain("another", "two"), nullptr);
  EXPECT_EQ(bindtextdomain("owned-domain", nullptr), dir);
  EXPECT_EQ(bind_textdomain_codeset("owned-domain", nullptr), code);
  EXPECT_STREQ(bindtextdomain("owned-domain", dir + 1), "ne");
  EXPECT_EQ(bind_textdomain_codeset("owned-domain", nullptr), code);
  EXPECT_EQ(bindtextdomain("owned-domain", ""), nullptr);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_STREQ(bindtextdomain("owned-domain", nullptr), "ne");
  EXPECT_EQ(bind_textdomain_codeset("owned-domain", ""), nullptr);
  EXPECT_EQ(int(libc_errno), EINVAL);
  EXPECT_EQ(bind_textdomain_codeset("owned-domain", nullptr), code);
}

#ifdef DOMAIN_BINDINGS_ALLOCATION_TEST
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

TEST(LlvmLibcDomainBindingsTest, AllocationRollbackAndAllocationFreeQueries) {
  for (unsigned failure = 1; failure <= 3; ++failure) {
    calls = 0;
    fail_call = failure;
    tracking = true;
    char *result = bindtextdomain("allocation-new", "directory");
    tracking = false;
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_EQ(calls, failure);
    EXPECT_EQ(live, 0U);
    EXPECT_STREQ(bindtextdomain("allocation-new", nullptr),
                 "/usr/share/locale");
  }
  char *dir = bindtextdomain("allocation-existing", "original");
  char *code = bind_textdomain_codeset("allocation-existing", "UTF-8");
  ASSERT_NE(dir, nullptr);
  ASSERT_NE(code, nullptr);
  for (unsigned field = 0; field != 2; ++field) {
    calls = 0;
    fail_call = 1;
    tracking = true;
    char *result = field
                       ? bind_textdomain_codeset("allocation-existing", "ASCII")
                       : bindtextdomain("allocation-existing", "replacement");
    tracking = false;
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_EQ(live, 0U);
    EXPECT_EQ(bindtextdomain("allocation-existing", nullptr), dir);
    EXPECT_EQ(bind_textdomain_codeset("allocation-existing", nullptr), code);
  }
  char *domain = textdomain("saved-domain");
  ASSERT_NE(domain, nullptr);
  calls = 0;
  fail_call = 1;
  tracking = true;
  char *result = textdomain("replacement-domain");
  tracking = false;
  EXPECT_EQ(result, nullptr);
  EXPECT_EQ(int(libc_errno), ENOMEM);
  EXPECT_EQ(textdomain(nullptr), domain);
  calls = 0;
  tracking = true;
  char *query = bind_textdomain_codeset("absent-query", nullptr);
  char *directory = bindtextdomain("absent-query", nullptr);
  result = textdomain(nullptr);
  tracking = false;
  EXPECT_EQ(calls, 0U);
  EXPECT_EQ(query, nullptr);
  EXPECT_STREQ(directory, "/usr/share/locale");
  EXPECT_EQ(result, domain);
  EXPECT_STREQ(textdomain(""), "messages");
}

TEST(LlvmLibcDomainBindingsTest, SnapshotFailurePreservesSelection) {
  using namespace LIBC_NAMESPACE::internal;
  DomainSnapshot saved;
  ASSERT_TRUE(snapshot_text_domain("snapshot-original", saved).has_value());
  calls = 0;
  fail_call = 1;
  tracking = true;
  auto result = snapshot_text_domain("snapshot-replacement", saved);
  tracking = false;
  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), ENOMEM);
  EXPECT_STREQ(saved.domain(), "snapshot-original");
  EXPECT_EQ(live, 0U);
}
#endif

#ifdef DOMAIN_BINDINGS_THREADS
TEST(LlvmLibcDomainBindingsTest,
     ConcurrentIndependentBindingsAndSharedQueries) {
  struct Input {
    const char *name;
    bool ok = true;
  } inputs[4] = {{"worker-0"}, {"worker-1"}, {"worker-2"}, {"worker-3"}};
  ASSERT_NE(bindtextdomain("shared-query", "shared"), nullptr);
  auto worker = +[](void *opaque) -> void * {
    auto &input = *static_cast<Input *>(opaque);
    for (unsigned i = 0; i != 32; ++i) {
      const char *value = i % 2 ? "first" : "second";
      char *result = bindtextdomain(input.name, value);
      input.ok &= result && LIBC_NAMESPACE::cpp::string_view(result) == value;
      result = bindtextdomain("shared-query", nullptr);
      input.ok &=
          result && LIBC_NAMESPACE::cpp::string_view(result) == "shared";
      input.ok &= bind_textdomain_codeset(input.name, nullptr) == nullptr;
      LIBC_NAMESPACE::internal::DomainSnapshot snapshot;
      input.ok &=
          LIBC_NAMESPACE::internal::snapshot_text_domain(input.name, snapshot)
              .has_value();
      input.ok &= snapshot.directory() && LIBC_NAMESPACE::cpp::string_view(
                                              snapshot.directory()) == value;
    }
    return nullptr;
  };
  pthread_t threads[4];
  unsigned count = 0;
  for (; count != 4; ++count) {
    int error = LIBC_NAMESPACE::pthread_create(&threads[count], nullptr, worker,
                                               &inputs[count]);
    EXPECT_EQ(error, 0);
    if (error)
      break;
  }
  for (unsigned i = 0; i != count; ++i) {
    EXPECT_EQ(LIBC_NAMESPACE::pthread_join(threads[i], nullptr), 0);
    EXPECT_TRUE(inputs[i].ok);
  }
}
#endif
