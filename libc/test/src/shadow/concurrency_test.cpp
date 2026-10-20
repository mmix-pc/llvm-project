//===----------------------------------------------------------------------===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//===----------------------------------------------------------------------===//

#include "src/__support/CPP/atomic.h"
#include "src/shadow/getspnam.h"
#include "src/shadow/getspnam_r.h"
#include "src/shadow/lookup.h"
#include "src/stdio/remove.h"
#include "test/UnitTest/Test.h"
#include <pthread.h>

namespace {
struct Reader {
  const char *name;
  const char *password;
  long last_change;
  int saved_errno;
  LIBC_NAMESPACE::cpp::Atomic<bool> *start;
  const char *failure = nullptr;
  unsigned iteration = 0;
  int query_status = 0;
  int observed_errno = 0;
  long observed_last_change = 0;
};

void *read_records(void *arg) {
  auto &reader = *static_cast<Reader *>(arg);
  while (!reader.start->load(LIBC_NAMESPACE::cpp::MemoryOrder::ACQUIRE)) {
  }
  spwd entry{}, *result;
  char buffer[128];
  auto fail = [&](const char *reason) -> void * {
    reader.failure = reason;
    reader.observed_last_change = entry.sp_lstchg;
    return arg;
  };
  for (unsigned i = 0; i < 500; ++i) {
    reader.iteration = i;
    libc_errno = reader.saved_errno;
    result = nullptr;
    reader.query_status = LIBC_NAMESPACE::getspnam_r(
        reader.name, &entry, buffer, sizeof(buffer), &result);
    reader.observed_errno = int(libc_errno);
    if (reader.query_status != 0)
      return fail("matched query returned an error");
    if (result != &entry)
      return fail(result ? "matched query returned the wrong entry pointer"
                         : "matched query returned no entry");
    if (reader.observed_errno != reader.saved_errno)
      return fail("matched query changed errno");
    if (!entry.sp_namp ||
        LIBC_NAMESPACE::cpp::string_view(entry.sp_namp) != reader.name)
      return fail("matched query returned the wrong name");
    if (!entry.sp_pwdp ||
        LIBC_NAMESPACE::cpp::string_view(entry.sp_pwdp) != reader.password)
      return fail("matched query returned the wrong password");
    if (entry.sp_lstchg != reader.last_change)
      return fail("matched query returned the wrong last-change value");
    reader.query_status = LIBC_NAMESPACE::getspnam_r("missing", &entry, buffer,
                                                     sizeof(buffer), &result);
    reader.observed_errno = int(libc_errno);
    if (reader.query_status != 0)
      return fail("missing query returned an error");
    if (result != nullptr)
      return fail("missing query returned an entry");
    if (reader.observed_errno != reader.saved_errno)
      return fail("missing query changed errno");
  }
  return nullptr;
}
} // namespace

TEST(LlvmLibcShadowConcurrencyTest, IndependentQueriesAndErrno) {
  // Keep the owning CString alive until every reader has joined.
  auto path = libc_make_test_file_path("shadow-concurrent.test");
  auto file = LIBC_NAMESPACE::openfile(path, "w");
  ASSERT_TRUE(file.has_value());
  const char records[] = "alice:!:123:0:90:7:30:20000:\n"
                         "bob:*:456:0:90:7:30:20000:\n";
  ASSERT_EQ(file.value()->write(records, sizeof(records) - 1).value,
            sizeof(records) - 1);
  ASSERT_EQ(file.value()->close(), 0);
  LIBC_NAMESPACE::shadow_db::TESTONLY_set_lookup_path(path);
  LIBC_NAMESPACE::cpp::Atomic<bool> start(false);
  Reader readers[] = {{"alice", "!", 123, EDOM, &start},
                      {"bob", "*", 456, ERANGE, &start},
                      {"alice", "!", 123, EINVAL, &start},
                      {"bob", "*", 456, EIO, &start}};
  pthread_t threads[4];
  unsigned created = 0;
  int create_error = 0;
  for (; created < 4; ++created) {
    create_error = ::pthread_create(&threads[created], nullptr, read_records,
                                    &readers[created]);
    if (create_error)
      break;
  }
  start.store(true, LIBC_NAMESPACE::cpp::MemoryOrder::RELEASE);
  // Only this thread uses the nonreentrant API; it must not affect _r readers.
  for (unsigned i = 0; i < 100; ++i) {
    auto *entry = LIBC_NAMESPACE::getspnam("alice");
    EXPECT_NE(entry, nullptr);
    if (entry)
      EXPECT_STREQ(entry->sp_namp, "alice");
  }
  for (unsigned i = 0; i < created; ++i) {
    void *result = &readers[i];
    int join_error = ::pthread_join(threads[i], &result);
    EXPECT_EQ(join_error, 0);
    // Report from the joining thread, after the worker's diagnostics are
    // stable.
    if (!join_error && readers[i].failure) {
      const auto &reader = readers[i];
      LIBC_NAMESPACE::testing::tlog
          << "shadow reader " << i << " (" << reader.name << "), iteration "
          << reader.iteration << ": " << reader.failure
          << "; query status=" << reader.query_status
          << "; errno=" << reader.observed_errno
          << " expected=" << reader.saved_errno
          << "; last-change=" << reader.observed_last_change
          << " expected=" << reader.last_change << '\n';
    }
    EXPECT_EQ(result, nullptr);
  }
  EXPECT_EQ(create_error, 0);
  LIBC_NAMESPACE::shadow_db::TESTONLY_set_lookup_path(nullptr);
  ASSERT_EQ(LIBC_NAMESPACE::remove(path), 0);
}
