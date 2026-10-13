//===-- Startup dependency search tests -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loader/linux/search.h"
#include "loader/linux/syscall.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>

using namespace LIBC_NAMESPACE::loader;
template <size_t N> static Bytes bytes(const char (&s)[N]) {
  return Bytes(reinterpret_cast<const unsigned char *>(s), N - 1);
}

struct FileModel {
  char attempted[32][DependencySearch::PATH_CAPACITY] = {};
  unsigned count = 0;
  const char *success = nullptr;
  const char *denied = nullptr;
  const char *broken = nullptr;
  long failure = -ENOEXEC, cleanup_error = 0;
  static MapResult load(void *context, const char *path) {
    auto &self = *static_cast<FileModel *>(context);
    assert(self.count < 32);
    strcpy(self.attempted[self.count++], path);
    if (self.success && !strcmp(path, self.success))
      return {0, 0};
    if (self.denied && !strcmp(path, self.denied))
      return {-EACCES, 0};
    if (self.broken && !strcmp(path, self.broken))
      return {self.failure, self.cleanup_error};
    return {-ENOENT, 0};
  }
};

static unsigned opens;
namespace LIBC_NAMESPACE_DECL {
namespace loader {
long raw_syscall(long number, long, long path, long, long, long, long) {
  assert(number == SYS_openat);
  const char *expected[] = {"/lib/libx.so", "/usr/lib/libx.so"};
  assert(opens < 2 &&
         !strcmp(reinterpret_cast<const char *>(path), expected[opens]));
  ++opens;
  return -ENOENT;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "line %d: %s\n", __LINE__, #condition);                  \
      return 1;                                                                \
    }                                                                          \
  } while (false)

extern "C" int main() {
  SearchEnvironment environment;
  const Bytes env[] = {bytes("HOME=/host/not/a/search/path"),
                       bytes("LD_LIBRARY_PATH=:/env::${ORIGIN}/env:"),
                       bytes("LD_PRELOAD=")};
  CHECK(environment.parse(false, env, bytes("/main")) == 0);
  SearchObject requester{bytes("/child"), bytes("$ORIGIN/rpath:/request"),
                         bytes("/run"), false};
  const SearchObject parents[] = {
      {bytes("/parent"), bytes("${ORIGIN}/lib"), {}, false},
      {bytes("/ignored"), bytes("/suppressed"), bytes("/not-inherited"), true},
      {bytes("/main"), bytes("$ORIGIN/lib"), {}, false}};
  FileModel files;
  MapResult result =
      DependencySearch::resolve(bytes("libx.so"), requester, parents,
                                environment, FileModel::load, &files);
  CHECK(result.error == -ENOENT && files.count == 8);
  const char *order[] = {"/child/rpath/libx.so", "/request/libx.so",
                         "/parent/lib/libx.so",  "/main/lib/libx.so",
                         "/env/libx.so",         "/main/env/libx.so",
                         "/lib/libx.so",         "/usr/lib/libx.so"};
  for (unsigned i = 0; i < files.count; ++i)
    CHECK(!strcmp(files.attempted[i], order[i]));
  for (unsigned winner = 0; winner < 8; ++winner) {
    files = FileModel{};
    files.success = order[winner];
    result = DependencySearch::resolve(bytes("libx.so"), requester, parents,
                                       environment, FileModel::load, &files);
    CHECK(!result.error && files.count == winner + 1);
  }
  requester.has_runpath = true;
  files = FileModel{};
  result = DependencySearch::resolve(bytes("libx.so"), requester, parents,
                                     environment, FileModel::load, &files);
  CHECK(result.error == -ENOENT && files.count == 5);
  CHECK(!strcmp(files.attempted[0], "/env/libx.so"));
  CHECK(!strcmp(files.attempted[1], "/main/env/libx.so"));
  CHECK(!strcmp(files.attempted[2], "/run/libx.so"));
  requester.runpath = {};
  files = FileModel{};
  CHECK(DependencySearch::resolve(bytes("libx.so"), requester, parents,
                                  environment, FileModel::load, &files)
            .error == -ENOENT);
  CHECK(files.count == 4); // Presence, not nonempty value, suppresses RPATH.

  const char *direct[] = {"/absolute/x.so", "relative/x.so", "./x.so",
                          "/child/x.so", "/child/x.so"};
  const Bytes names[] = {bytes("/absolute/x.so"), bytes("relative/x.so"),
                         bytes("./x.so"), bytes("$ORIGIN/x.so"),
                         bytes("${ORIGIN}/x.so")};
  for (unsigned i = 0; i < 5; ++i) {
    files = FileModel{};
    CHECK(DependencySearch::resolve(names[i], requester, parents, environment,
                                    FileModel::load, &files)
              .error == -ENOENT);
    CHECK(files.count == 1 && !strcmp(files.attempted[0], direct[i]));
  }

  CHECK(environment.parse(false, {}, {}) == 0);
  // Missing main provenance disables only paths requiring its ORIGIN.
  const Bytes optional_origin[] = {
      bytes("LD_LIBRARY_PATH=$ORIGIN/env:/absolute:${ORIGIN}/other")};
  CHECK(environment.parse(false, optional_origin, {}) == 0);
  requester = {{}, bytes("$ORIGIN/rpath:/rpath"), {}, false};
  files = FileModel{};
  CHECK(DependencySearch::resolve(bytes("libx.so"), requester, {}, environment,
                                  FileModel::load, &files).error == -ENOENT);
  CHECK(files.count == 4);
  CHECK(!strcmp(files.attempted[0], "/rpath/libx.so"));
  CHECK(!strcmp(files.attempted[1], "/absolute/libx.so"));
  CHECK(!strcmp(files.attempted[2], "/lib/libx.so"));
  CHECK(!strcmp(files.attempted[3], "/usr/lib/libx.so"));
  requester.has_runpath = true;
  requester.runpath = bytes("$ORIGIN/run:/run");
  files = FileModel{};
  files.success = "/run/libx.so";
  CHECK(!DependencySearch::resolve(bytes("libx.so"), requester, {}, environment,
                                   FileModel::load, &files).error);
  CHECK(files.count == 2);
  requester.origin = bytes("/dso");
  files = FileModel{};
  files.success = "/dso/run/libx.so";
  CHECK(!DependencySearch::resolve(bytes("libx.so"), requester, {}, environment,
                                   FileModel::load, &files).error);
  CHECK(files.count == 2); // Main origin is unknown, DSO origin is not.
  const Bytes hidden_invalid[] = {bytes("LD_LIBRARY_PATH=$ORIGIN/$LIB")};
  CHECK(environment.parse(false, hidden_invalid, {}) == -ENOTSUP);
  CHECK(environment.parse(false, {}, {}) == 0);
  requester = {{}, {}, {}, false};
  files = FileModel{};
  files.denied = "/lib/libx.so";
  CHECK(DependencySearch::resolve(bytes("libx.so"), requester, {}, environment,
                                  FileModel::load, &files)
                .error == -EACCES &&
        files.count == 2);
  files = FileModel{};
  files.denied = "/lib/libx.so";
  files.success = "/usr/lib/libx.so";
  CHECK(DependencySearch::resolve(bytes("libx.so"), requester, {}, environment,
                                  FileModel::load, &files)
                .error == 0 &&
        files.count == 2);
  const long errors[] = {-ENOEXEC, -ENOMEM, -EIO, -EBUSY, -ENOTDIR};
  for (long error : errors) {
    files = FileModel{};
    files.broken = "/lib/libx.so";
    files.failure = error;
    result = DependencySearch::resolve(bytes("libx.so"), requester, {},
                                       environment, FileModel::load, &files);
    CHECK(result.error == (error == -ENOTDIR ? -ENOENT : error));
    CHECK(files.count == (error == -ENOTDIR ? 2u : 1u));
  }
  files = FileModel{};
  files.broken = "/lib/libx.so";
  files.failure = -ENOENT;
  files.cleanup_error = -EIO;
  result = DependencySearch::resolve(bytes("libx.so"), requester, {},
                                     environment, FileModel::load, &files);
  CHECK(result.error == -ENOENT && result.cleanup_error == -EIO &&
        files.count == 1);

  const Bytes invalid[] = {bytes("$LIB/x.so"),
                           bytes("${PLATFORM}/x.so"),
                           bytes("$ORIGINextra/x.so"),
                           bytes("${ORIGIN/x.so"),
                           bytes("$"),
                           bytes("x\0y")};
  for (Bytes name : invalid) {
    files = FileModel{};
    CHECK(DependencySearch::resolve(name, requester, {}, environment,
                                    FileModel::load, &files)
                  .error != 0 &&
          files.count == 0);
  }
  files = FileModel{};
  CHECK(DependencySearch::resolve(bytes("$ORIGIN/x.so"), requester, {},
                                  environment, FileModel::load, &files)
                .error == -ENOENT &&
        !files.count);
  requester.origin = bytes("relative");
  CHECK(DependencySearch::resolve(bytes("${ORIGIN}/x.so"), requester, {},
                                  environment, FileModel::load, &files)
                .error == -EINVAL &&
        !files.count);
  requester.rpath = bytes("/first:$LIB/late");
  files.success = "/first/libx.so";
  CHECK(DependencySearch::resolve(bytes("libx.so"), requester, {}, environment,
                                  FileModel::load, &files)
                .error == -ENOTSUP &&
        !files.count);

  char huge[DependencySearch::PATH_CAPACITY + 1];
  memset(huge, 'a', sizeof(huge));
  huge[sizeof(huge) - 1] = 0;
  requester = {{}, {}, {}, false};
  CHECK(DependencySearch::resolve(bytes(huge), requester, {}, environment,
                                  FileModel::load, &files)
                .error == -ENAMETOOLONG &&
        !files.count);
  requester.rpath =
      Bytes(reinterpret_cast<unsigned char *>(huge), sizeof(huge) - 2);
  CHECK(DependencySearch::resolve(bytes("x"), requester, {}, environment,
                                  FileModel::load, &files)
                .error == -ENAMETOOLONG &&
        !files.count);

  // A rejected environment cannot be used to load even a direct pathname.
  const Bytes rejected[] = {
      bytes("LD_PRELOAD=x"), bytes("LD_AUDIT=x"), bytes("LD_DEBUG=all"),
      bytes("LD_LIBRARY_PATH=$LIB"), bytes("LD_LIBRARY_PATH=x\0y")};
  for (Bytes entry : rejected) {
    CHECK(environment.parse(false, {&entry, 1}, bytes("/main")) != 0);
    CHECK(!environment.admitted());
    CHECK(DependencySearch::resolve(bytes("/direct.so"), requester, {},
                                    environment, FileModel::load, &files)
                  .error == -EINVAL &&
          !files.count);
  }
  const Bytes preload[] = {bytes("LD_PRELOAD=libinject.so")};
  CHECK(environment.parse(false, preload, {}) == -ENOTSUP);
  CHECK(environment.rejected_setting().size() == sizeof("LD_PRELOAD") - 1);
  CHECK(!memcmp(environment.rejected_setting().data(), "LD_PRELOAD",
                sizeof("LD_PRELOAD") - 1));
  const Bytes duplicate[] = {bytes("LD_LIBRARY_PATH=/a"),
                             bytes("LD_LIBRARY_PATH=/b")};
  CHECK(environment.parse(false, duplicate, {}) == -EINVAL);
  // An unreadable view is never touched on the secure rejection path.
  CHECK(environment.parse(true, {reinterpret_cast<const Bytes *>(1), 1}, {}) ==
        -ENOTSUP);
  CHECK(environment.parse(false, {}, {}) == 0);
  requester = {{}, bytes(":::"), {}, false};
  DsoMapping owner(8192);
  CHECK(DependencySearch::load(bytes("libx.so"), requester, {}, environment,
                               owner)
            .error == -ENOENT);
  CHECK(opens == 2 && !owner.image() && owner.reset() == 0);
  puts("loader dependency search order and rejection checks passed");
  return 0;
}
