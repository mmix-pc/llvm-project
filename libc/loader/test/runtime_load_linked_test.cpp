//===-- Native late loading of linked MMIX ELF
//-----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "dynamic_fixture.h"
#include "loader/linux/runtime_load.h"
#include "loader/linux/syscall.h"
#include "loader/linux/target.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace loader_test;
namespace LIBC_NAMESPACE_DECL {
namespace loader {
long raw_syscall(long n, long a, long b, long c, long d, long e, long f) {
  // This native x86-64 transport keeps actual mappings in MMIX's user range.
  // It does not execute target code or model guest syscall ABI behavior.
  if (n == SYS_mmap && !(d & MAP_FIXED))
    d |= MAP_32BIT;
  long result = ::syscall(n, a, b, c, d, e, f);
  return result == -1 ? -errno : result;
}
} // namespace loader
} // namespace LIBC_NAMESPACE_DECL

static void check(bool ok, int line = __builtin_LINE()) {
  if (!ok) {
    std::fprintf(stderr, "runtime linked check failed at %d\n", line);
    std::abort();
  }
}
static Bytes text(const char *s) {
  return {reinterpret_cast<const unsigned char *>(s), std::strlen(s)};
}
static Address octa(const unsigned char *p) {
  Address v = 0;
  for (unsigned i = 0; i < 8; ++i)
    v = (v << 8) | p[i];
  return v;
}

int main(int argc, char **argv) {
  check(argc == 4); // GD, IE and missing-strong-symbol DSOs, absolute paths.
  DynamicFixture main_file(ImageRole::MainPIE);
  MappedRegion regions[] = {{0, {main_file.bytes, page}},
                            {page, {main_file.bytes + page, page}},
                            {2 * page, {main_file.bytes + 2 * page, page}}};
  ElfImage main;
  check(ElfImage::open_mapped(regions, 0, 64, ImageRole::MainPIE, bias, page,
                              main) == ElfError::None);
  ImageRegistry registry(page);
  ImageRegistry::Transaction initial(page);
  const ElfImage *images[] = {&main};
  ImageRegistry::Discovery discovery[] = {
      {false, {}, text("/test/main"), ~size_t(0), {}}};
  check(!registry.prepare(initial, images, discovery));
  check(initial.mark_relocated() && initial.mark_tls_ready() &&
        initial.mark_protected() && !registry.publish(initial));
  RuntimeScopes scopes(page);
  RuntimeScopes::Transaction startup_scopes(page);
  check(!scopes.prepare_startup(*registry.snapshot(), startup_scopes));
  check(!scopes.publish(startup_scopes));
  const auto *old = scopes.snapshot();
  const auto *caller = old->registry->images[0];
  SearchEnvironment environment;
  check(!environment.parse(false, {}, {}));
  for (int i = 2; i < 4; ++i) {
    RuntimeLoad rejected(page);
    auto result = rejected.prepare(registry, scopes, *caller, text(argv[i]),
                                   environment, false);
    check(result.error == -ENOEXEC && !result.cleanup_error &&
          !rejected.view());
    check(scopes.snapshot() == old && registry.snapshot() == old->registry);
    check(!rejected.abort());
  }
  RuntimeLoad load(page);
  auto result = load.prepare(registry, scopes, *caller, text(argv[1]),
                             environment, false);
  check(!result.error && !result.cleanup_error && load.view());
  const auto *image = load.view()->registry->images[1];
  check(!image->startup && image->tls.module_id > 1 &&
        image->tls.generation > 1 && !image->tls.tp_offset);
  DynamicInfo info;
  check(DynamicInfo::open(image->elf, info) == DynamicError::None);
  bool module_seen = false, offset_seen = false, template_seen = false;
  for (Address i = 0; i < info.relocation_count(); ++i) {
    DynamicRelocation r;
    check(info.relocation(i, r));
    RuntimeScopes::Value value;
    Address expected = image->elf.load_bias();
    if (r.info >> 32) {
      check(RuntimeScopes::resolve_binding(*load.view(), image, r.info >> 32,
                                           value) == SymbolError::None);
      expected = value.symbol.address;
    }
    if (is_tls_relocation(r.info)) {
      module_seen |= is_tls_module_relocation(r.info);
      offset_seen |= !is_tls_module_relocation(r.info);
      const auto &tls = (r.info >> 32) ? value.image->tls : image->tls;
      check(tls_relocation_value(r.info, tls.module_id, 0, tls.memory_size,
                                 (r.info >> 32) ? expected : 0, r.addend,
                                 expected));
    } else {
      check(runtime_relocation_value(r.info, expected, r.addend, expected));
      Address runtime = image->elf.load_bias() + r.offset;
      template_seen |=
          runtime >= image->tls.template_address &&
          runtime - image->tls.template_address < image->tls.file_size;
    }
    Bytes bytes = image->elf.mapped_memory_bytes(r.offset, 8, PF_R);
    check(bytes.size() == 8 && octa(bytes.data()) == expected);
  }
  check(module_seen && offset_seen && template_seen);
  // Actual native page permissions, not a mocked mprotect success.
  bool relro_seen = false;
  for (unsigned i = 0; i < image->elf.header()->e_phnum; ++i) {
    Elf64_Phdr p;
    check(image->elf.program_header(i, p));
    if (p.p_type != PT_GNU_RELRO || !p.p_memsz)
      continue;
    relro_seen = true;
    pid_t child = fork();
    check(child >= 0);
    if (!child) {
      *reinterpret_cast<volatile unsigned char *>(image->elf.load_bias() +
                                                  p.p_vaddr) = 0;
      _exit(0);
    }
    int status;
    check(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
          WTERMSIG(status) == SIGSEGV);
  }
  check(relro_seen && scopes.snapshot() == old);
  uint64_t token;
  check(!load.commit(token) && token);
  check(scopes.snapshot()->registry == registry.snapshot());
  RuntimeLoad reopen(page);
  check(!reopen
             .prepare(registry, scopes, *caller, text(argv[1]), environment,
                      true, true)
             .error);
  check(!reopen.commit(token));
  check(registry.snapshot()->images[1] == image &&
        old->registry->images.size() == 1);
  std::puts("linked late GD relocation, immutable template, IE/undefined "
            "rejection, RELRO and reopen passed");
}
