//===-- Linux numbered catalog lifecycle tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "config/app.h"
#include "hdr/fcntl_macros.h"
#include "hdr/locale_macros.h"
#include "hdr/sys_stat_macros.h"
#include "include/llvm-libc-macros/nl_types-macros.h"
#include "src/__support/CPP/string.h"
#include "src/__support/OSUtil/linux/auxv.h"
#include "src/__support/libc_errno.h"
#include "src/locale/freelocale.h"
#include "src/locale/newlocale.h"
#include "src/locale/uselocale.h"
#include "src/nl_types/catclose.h"
#include "src/nl_types/catgets.h"
#include "src/nl_types/catopen.h"
#include "src/stdlib/environ_internal.h"
#include "src/stdlib/mkdtemp.h"
#include "test/UnitTest/Test.h"

namespace LIBC_NAMESPACE_DECL {
AppProperties app;
}

#ifdef CATALOG_OPEN_ALLOCATION_TEST
namespace {
bool tracking = false;
unsigned allocations = 0, fail_at = 0, live = 0;
void *tracked[8]{};
} // namespace
extern "C" void *__real_malloc(size_t);
extern "C" void __real_free(void *);
extern "C" void *__wrap_malloc(size_t size) {
  if (tracking && ++allocations == fail_at)
    return nullptr;
  void *p = __real_malloc(size);
  if (tracking && p)
    for (auto &slot : tracked)
      if (!slot) {
        slot = p;
        ++live;
        break;
      }
  return p;
}
extern "C" void __wrap_free(void *p) {
  if (p)
    for (auto &slot : tracked)
      if (slot == p) {
        slot = nullptr;
        --live;
        break;
      }
  __real_free(p);
}
#endif

namespace {
using namespace LIBC_NAMESPACE;
auxv::Entry security[] = {{AT_SECURE, 0}, {AT_NULL, 0}};
void word(uint8_t *p, uint32_t v, bool big) {
  for (unsigned i = 0; i != 4; ++i)
    p[i] = uint8_t(v >> (8 * (big ? 3 - i : i)));
}
class LlvmLibcCatalogLinuxTest : public testing::Test {
protected:
  char root[32] = "catalog-XXXXXX";
  cpp::string path(const char *name) {
    cpp::string p(root);
    p += "/";
    p += name;
    return p;
  }
  void write_catalog(const char *name, bool valid = true) {
    auto p = path(name);
    int fd = syscall_impl<int>(SYS_openat, AT_FDCWD, p.c_str(),
                               O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    ASSERT_GE(fd, 0);
    uint8_t bytes[42]{};
    word(bytes, valid ? 0x960408de : 0, false);
    word(bytes + 4, 1, false);
    word(bytes + 8, 1, false);
    word(bytes + 12, 2, false);
    word(bytes + 16, 1, false);
    word(bytes + 24, 2, true);
    word(bytes + 28, 1, true);
    const char text[] = "hello";
    for (size_t i = 0; i != sizeof(text); ++i)
      bytes[36 + i] = text[i];
    EXPECT_EQ(syscall_impl<long>(SYS_write, fd, bytes, sizeof(bytes)),
              long(sizeof(bytes)));
    EXPECT_EQ(syscall_impl<long>(SYS_close, fd), 0L);
  }
  void SetUp() override {
    security[0].val = 0;
    auxv::Vector::initialize_unsafe(security);
    ASSERT_NE(LIBC_NAMESPACE::mkdtemp(root), nullptr);
  }
  void TearDown() override {
    internal::EnvironmentManager::get_instance().clear();
    const char *names[] = {"app", "bad", "C", "C.UTF-8", "lang", "pipe"};
    for (auto name : names)
      syscall_impl<long>(SYS_unlinkat, AT_FDCWD, path(name).c_str(), 0);
    syscall_impl<long>(SYS_unlinkat, AT_FDCWD, root, AT_REMOVEDIR);
  }
};

TEST_F(LlvmLibcCatalogLinuxTest, LiteralLookupAndOwnedLifetime) {
  write_catalog("app");
  libc_errno = EDOM;
  nl_catd cat = LIBC_NAMESPACE::catopen(path("app").c_str(), NL_CAT_LOCALE);
  ASSERT_NE(cat, reinterpret_cast<nl_catd>(-1));
  EXPECT_EQ(int(libc_errno), EDOM);
  ASSERT_EQ(syscall_impl<long>(SYS_unlinkat, AT_FDCWD, path("app").c_str(), 0),
            0L);
  char fallback[] = "fallback";
  EXPECT_STREQ(LIBC_NAMESPACE::catgets(cat, 1, 1, fallback), "hello");
  EXPECT_EQ(int(libc_errno), EDOM);
  EXPECT_EQ(LIBC_NAMESPACE::catgets(cat, 1, 2, fallback), fallback);
  EXPECT_EQ(int(libc_errno), ENOMSG);
  EXPECT_STREQ(LIBC_NAMESPACE::catgets(cat, 1, 1, fallback), "hello");
  EXPECT_EQ(LIBC_NAMESPACE::catclose(cat), 0);
}

TEST_F(LlvmLibcCatalogLinuxTest, SearchAndSelectedCategory) {
  write_catalog("bad", false);
  write_catalog("C.UTF-8");
  write_catalog("lang");
  auto &env = internal::EnvironmentManager::get_instance();
  cpp::string paths = path("bad");
  paths += ":";
  paths += path("%L");
  ASSERT_EQ(env.set("NLSPATH", paths.c_str(), true), 0);
  ASSERT_EQ(env.set("LANG", "lang", true), 0);
  locale_t locale =
      LIBC_NAMESPACE::newlocale(LC_MESSAGES_MASK, "C.UTF-8", nullptr);
  ASSERT_NE(locale, nullptr);
  locale_t old = LIBC_NAMESPACE::uselocale(locale);
  nl_catd cat = LIBC_NAMESPACE::catopen("app", NL_CAT_LOCALE);
  EXPECT_NE(cat, reinterpret_cast<nl_catd>(-1));
  if (cat != reinterpret_cast<nl_catd>(-1))
    EXPECT_EQ(LIBC_NAMESPACE::catclose(cat), 0);
  LIBC_NAMESPACE::uselocale(old);
  LIBC_NAMESPACE::freelocale(locale);
  cat = LIBC_NAMESPACE::catopen("app", 0);
  EXPECT_NE(cat, reinterpret_cast<nl_catd>(-1));
  if (cat != reinterpret_cast<nl_catd>(-1))
    EXPECT_EQ(LIBC_NAMESPACE::catclose(cat), 0);
}

TEST_F(LlvmLibcCatalogLinuxTest, SecurityAndNonregularFiles) {
  write_catalog("app");
  auto &env = internal::EnvironmentManager::get_instance();
  ASSERT_EQ(env.set("NLSPATH", path("app").c_str(), true), 0);
  security[0].val = 1;
  EXPECT_EQ(LIBC_NAMESPACE::catopen("not-an-installed-catalog", 0),
            reinterpret_cast<nl_catd>(-1));
  nl_catd cat = LIBC_NAMESPACE::catopen(path("app").c_str(), 0);
  ASSERT_NE(cat, reinterpret_cast<nl_catd>(-1));
  EXPECT_EQ(LIBC_NAMESPACE::catclose(cat), 0);
  ASSERT_EQ(syscall_impl<long>(SYS_mknodat, AT_FDCWD, path("pipe").c_str(),
                               S_IFIFO | 0600, 0),
            0L);
  EXPECT_EQ(LIBC_NAMESPACE::catopen(path("pipe").c_str(), 0),
            reinterpret_cast<nl_catd>(-1));
  EXPECT_EQ(int(libc_errno), ENOENT);
}

#ifdef CATALOG_OPEN_ALLOCATION_TEST
TEST_F(LlvmLibcCatalogLinuxTest, AllocationFailureReleasesFileAndStorage) {
  write_catalog("app");
  write_catalog("bad", false);
  auto filename = path("app");
  int next_fd = syscall_impl<int>(SYS_openat, AT_FDCWD, filename.c_str(),
                                  O_RDONLY | O_CLOEXEC, 0);
  ASSERT_GE(next_fd, 0);
  ASSERT_EQ(syscall_impl<long>(SYS_close, next_fd), 0L);
  for (unsigned failure = 1; failure <= 4; ++failure) {
    allocations = 0;
    fail_at = failure;
    tracking = true;
    nl_catd cat = LIBC_NAMESPACE::catopen(filename.c_str(), 0);
    tracking = false;
    EXPECT_EQ(cat, reinterpret_cast<nl_catd>(-1));
    EXPECT_EQ(int(libc_errno), ENOMEM);
    EXPECT_EQ(live, 0U);
    int probe = syscall_impl<int>(SYS_openat, AT_FDCWD, filename.c_str(),
                                  O_RDONLY | O_CLOEXEC, 0);
    EXPECT_EQ(probe, next_fd);
    if (probe >= 0)
      syscall_impl<long>(SYS_close, probe);
  }
  allocations = fail_at = 0;
  tracking = true;
  nl_catd cat = LIBC_NAMESPACE::catopen(filename.c_str(), 0);
  tracking = false;
  ASSERT_NE(cat, reinterpret_cast<nl_catd>(-1));
  EXPECT_EQ(live, 3U);
  EXPECT_EQ(LIBC_NAMESPACE::catclose(cat), 0);
  EXPECT_EQ(live, 0U);
  filename = path("bad");
  allocations = 0;
  tracking = true;
  cat = LIBC_NAMESPACE::catopen(filename.c_str(), 0);
  tracking = false;
  EXPECT_EQ(cat, reinterpret_cast<nl_catd>(-1));
  EXPECT_EQ(int(libc_errno), ENOENT);
  EXPECT_EQ(live, 0U);
}
#endif
} // namespace
