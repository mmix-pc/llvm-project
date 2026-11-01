//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// REQUIRES: target={{x86_64-.*linux.*}}
// RUN: %{cxx} %s -I%S/../include -std=c++17 -fno-exceptions -fno-rtti -pthread -ldl -O0 -o %t.exe
// RUN: %{exec} %t.exe
// RUN: %{cxx} %s -I%S/../include -std=c++17 -fno-exceptions -fno-rtti -pthread -ldl -O2 -o %t.opt.exe
// RUN: %{exec} %t.opt.exe

// Compile the real provider and cursor/parser. Instrument only its cache locks;
// no public signal/cancellation wrapper or target section provider is mocked
// into a claim of handler-cancellation qualification.
#undef NDEBUG
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned cacheReads, cacheWrites;
static int cacheRead(pthread_rwlock_t *) { ++cacheReads; return 0; }
static int cacheWrite(pthread_rwlock_t *) { ++cacheWrites; return 0; }
static int cacheUnlock(pthread_rwlock_t *) { return 0; }
#define pthread_rwlock_rdlock cacheRead
#define pthread_rwlock_wrlock cacheWrite
#define pthread_rwlock_unlock cacheUnlock
#define _LIBUNWIND_IS_NATIVE_ONLY
#define _LIBUNWIND_USE_METADATA_POLICY
#include "../src/UnwindMetadataPolicy.cpp"
#include "../src/libunwind.cpp"
#undef pthread_rwlock_rdlock
#undef pthread_rwlock_wrlock
#undef pthread_rwlock_unlock

static jmp_buf resumed;
namespace libunwind {
extern "C" void __libunwind_Registers_x86_64_jumpto(Registers_x86_64 *) {
  // Simulate the nonreturning transfer, deliberately skipping C++ destructors.
  longjmp(resumed, 1);
}
} // namespace libunwind

struct Bytes {
  unsigned char data[256] = {};
  size_t size = 0;
  void byte(unsigned char value) { data[size++] = value; }
  void word(uint32_t value) {
    memcpy(data + size, &value, sizeof(value)); size += sizeof(value);
  }
  void pointer(uintptr_t value) {
    memcpy(data + size, &value, sizeof(value)); size += sizeof(value);
  }
  void end(size_t start) {
    while (size % 8) byte(DW_CFA_nop);
    uint32_t length = size - start - 4;
    memcpy(data + start, &length, sizeof(length));
  }
};

struct AddressSpace : libunwind::LocalAddressSpace {
  libunwind::UnwindInfoSections sections = {};
  bool visible = true;
  uintptr_t otherFDE = 0;
  unsigned otherCalls = 0;
  template <typename R>
  bool findUnwindSections(typename R::link_hardened_reg_arg_t,
                         libunwind::UnwindInfoSections &out) {
    out = sections;
    return visible;
  }
  template <typename R>
  bool findOtherFDE(typename R::link_hardened_reg_arg_t, uintptr_t &out) {
    ++otherCalls;
    out = otherFDE;
    return out != 0;
  }
};

using Registers = libunwind::Registers_x86_64;
using Cursor = libunwind::UnwindCursor<AddressSpace, Registers>;
using Cache = libunwind::DwarfFDECache<AddressSpace>;
struct PreviousLayout : libunwind::AbstractUnwindCursor {
  AddressSpace &as;
  Registers registers;
  unw_proc_info_t info;
  bool missing;
  bool signal;
};
static_assert(sizeof(Cursor) == sizeof(PreviousLayout), "cursor grew");
static_assert(alignof(Cursor) == alignof(PreviousLayout), "cursor alignment");
static_assert(sizeof(Cursor) == sizeof(unw_cursor_t), "public storage changed");

static unw_context_t seed(uintptr_t pc, uintptr_t sp = 0) {
  Registers registers;
  registers.setIP(pc);
  registers.setSP(sp);
  unw_context_t result = {};
  memcpy(&result, &registers, sizeof(registers));
  return result;
}

static void expectMissing(Cursor &cursor) {
  unw_proc_info_t info;
  cursor.getInfo(&info);
  assert(!info.end_ip);
  assert(cursor.step() == UNW_EBADFRAME);
  assert(cursor.step(true) == UNW_EBADFRAME);
}

static void expectTrap(void (*fn)()) {
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    struct rlimit limit = {0, 0};
    assert(setrlimit(RLIMIT_CORE, &limit) == 0);
    fn();
    _exit(99);
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGILL);
}

static void terminalMiss() {
  UnwindMetadataPolicyToken token{};
  assert(__unw_metadata_policy_enter(&token));
  assert(__unw_metadata_policy_commit(&token));
  AddressSpace as;
  as.visible = false;
  auto context = seed(0x1000);
  Cursor cursor(&context, as, true);
  cursor.setInfoBasedOnIPRegister();
}

static void preflightResume() {
  UnwindMetadataPolicyToken token{};
  assert(__unw_metadata_policy_enter(&token));
  AddressSpace as;
  auto context = seed(0);
  Cursor cursor(&context, as);
  cursor.jumpto();
}

static void endedScopeResume() {
  UnwindMetadataPolicyToken token{};
  assert(__unw_metadata_policy_enter(&token));
  assert(__unw_metadata_policy_commit(&token));
  AddressSpace as;
  auto context = seed(0);
  Cursor cursor(&context, as);
  assert(__unw_metadata_policy_leave(&token));
  cursor.jumpto();
}

static void commitFailedCursor() {
  UnwindMetadataPolicyToken token{};
  assert(__unw_metadata_policy_enter(&token));
  AddressSpace as;
  as.visible = false;
  auto context = seed(0x1000);
  Cursor cursor(&context, as, true);
  cursor.setInfoBasedOnIPRegister();
  expectMissing(cursor);
  assert(__unw_metadata_policy_commit(&token));
  unw_proc_info_t info;
  cursor.getInfo(&info);
}

static void *peer(void *opaque) {
  auto *outer = static_cast<UnwindMetadataPolicyToken *>(opaque);
  assert(__unw_metadata_policy_current() == UNW_METADATA_ORDINARY);
  assert(!__unw_metadata_policy_commit(outer));
  UnwindMetadataPolicyToken token{};
  assert(__unw_metadata_policy_enter(&token));
  assert(__unw_metadata_policy_commit(&token));
  assert(__unw_metadata_policy_leave(&token));
  return nullptr;
}

int main() {
  Bytes eh;
  eh.word(0); eh.word(0); eh.byte(1); eh.byte(0);
  eh.byte(1); eh.byte(0x78); eh.byte(16);
  eh.byte(DW_CFA_def_cfa); eh.byte(7); eh.byte(8);
  eh.byte(DW_CFA_offset | 16); eh.byte(1);
  eh.end(0);
  size_t fde = eh.size;
  eh.word(0); eh.word(eh.size);
  eh.pointer(0x1000); eh.pointer(8); eh.end(fde); eh.word(0);
  uintptr_t fdeAddress = (uintptr_t)(eh.data + fde);
  Bytes index;
  index.byte(1); index.byte(DW_EH_PE_absptr);
  index.byte(DW_EH_PE_udata4); index.byte(DW_EH_PE_udata8);
  index.pointer((uintptr_t)eh.data); index.word(1);
  index.pointer(0x1000); index.pointer(fdeAddress);
  AddressSpace as;
  as.sections.dso_base = 0x1000;
  as.sections.dwarf_section = (uintptr_t)eh.data;
  as.sections.dwarf_section_length = eh.size;
  uintptr_t stack[] = {0x2004};
  auto context = seed(0x1004, (uintptr_t)stack);
  unw_proc_info_t info;

  // Ordinary section scan and insertion still work.
  Cursor ordinary(&context, as);
  ordinary.setInfoBasedOnIPRegister();
  ordinary.getInfo(&info);
  assert(info.start_ip == 0x1000 && cacheReads && cacheWrites);
  Cache::removeAllIn(0x1000);
  // Install a matching registered FDE: strict lookup must not use it.
  Cache::add(0x3000, 0x1000, 0x1008, fdeAddress);

  UnwindMetadataPolicyToken outer{}, inner{};
  assert(__unw_metadata_policy_enter(&outer));
  assert(!__unw_metadata_policy_enter(&outer));
  assert(__unw_metadata_policy_enter(&inner));
  assert(!__unw_metadata_policy_leave(&outer));
  assert(__unw_metadata_policy_leave(&inner));
  assert(!__unw_metadata_policy_leave(&inner));
  pthread_t thread;
  assert(pthread_create(&thread, nullptr, peer, &outer) == 0);
  assert(pthread_join(thread, nullptr) == 0);
  assert(__unw_metadata_policy_current() == UNW_METADATA_PREFLIGHT);
  cacheReads = cacheWrites = 0;

  // No index: neither section cache nor full scan/cache insertion is allowed.
  Cursor noIndex(&context, as, true);
  noIndex.setInfoBasedOnIPRegister();
  expectMissing(noIndex);
  assert(cacheReads == 0 && cacheWrites == 0 && as.otherCalls == 0);
  // No section: neither global registered cache nor platform fallback runs.
  as.visible = false;
  as.otherFDE = fdeAddress;
  Cursor noSection(&context, as, true);
  noSection.setInfoBasedOnIPRegister();
  expectMissing(noSection);
  assert(cacheReads == 0 && cacheWrites == 0 && as.otherCalls == 0);
  as.visible = true;
  as.sections.dwarf_index_section = (uintptr_t)index.data;
  as.sections.dwarf_index_section_length = index.size;

  // An empty/malformed index must not reactivate the section-scan fallback.
  as.sections.dwarf_index_section_length = 0;
  Cursor emptyIndex(&context, as, true);
  emptyIndex.setInfoBasedOnIPRegister();
  expectMissing(emptyIndex);
  assert(cacheReads == 0 && cacheWrites == 0 && as.otherCalls == 0);
  as.sections.dwarf_index_section_length = index.size;

  Cursor indexed(&context, as, true);
  indexed.setInfoBasedOnIPRegister();
  indexed.getInfo(&info);
  assert(info.start_ip == 0x1000);
  assert(cacheReads == 0 && cacheWrites == 0);
  // A successful step whose next PC has no index entry is an error, not end.
  Cursor copied(indexed);
  assert(copied.step(true) == UNW_EBADFRAME);
  assert(cacheReads == 0 && cacheWrites == 0 && as.otherCalls == 0);
  // Previously ordinary cursors cannot smuggle registered metadata into a scope.
  expectMissing(ordinary);

  // Exercise the real initializer/copy entrypoints as well as the template.
  auto missingContext = seed(0x4000);
  unw_cursor_t publicCursor, publicCopy;
  assert(__unw_init_local(&publicCursor, &missingContext) == UNW_ESUCCESS);
  assert(__unw_get_proc_info(&publicCursor, &info) == UNW_ENOINFO);
  assert(__unw_step(&publicCursor) == UNW_EBADFRAME);
  assert(__unw_copy_cursor(&publicCopy, &publicCursor) == UNW_ESUCCESS);
#if defined(_LIBUNWIND_SUPPORT_SAVED_CONTEXT)
  unw_cursor_t current;
  assert(__unw_init_local_current(&current, &missingContext) == UNW_ESUCCESS);
  assert(__unw_get_proc_info(&current, &info) == UNW_ENOINFO);
  assert(__unw_step_stage2(&current) == UNW_EBADFRAME);
  __unw_destroy_cursor(&current);
#endif
  assert(__unw_metadata_policy_leave(&outer));
  assert(__unw_step_stage2(&publicCopy) == UNW_EBADFRAME);
  __unw_destroy_cursor(&publicCursor);
  __unw_destroy_cursor(&publicCopy);
  // Copies carry the restriction and sticky failure after preflight returns.
  Cursor failedCopy(noSection);
  expectMissing(failedCopy);
  assert(cacheReads == 0 && cacheWrites == 0 && as.otherCalls == 0);
  // Fresh ordinary cursors retain both old fallbacks.
  as.visible = false;
  Cursor registered(&context, as);
  registered.setInfoBasedOnIPRegister();
  registered.getInfo(&info);
  assert(info.start_ip == 0x1000 && cacheReads != 0);
  Cache::removeAllIn(0x3000);
  Cursor platform(&context, as);
  platform.setInfoBasedOnIPRegister();
  platform.getInfo(&info);
  assert(info.start_ip == 0x1000 && as.otherCalls == 1);

  expectTrap(terminalMiss);
  expectTrap(preflightResume);
  expectTrap(endedScopeResume);
  expectTrap(commitFailedCursor);

  // A token stored outside an abandoned frame can end the scope after resume.
  // New seeded cursors inherit terminal policy; TLS has no retired-stack link.
  static UnwindMetadataPolicyToken continuation;
  alignas(Cursor) static unsigned char landingStorage[sizeof(Cursor)];
  as.visible = true;
  assert(__unw_metadata_policy_enter(&continuation));
  assert(__unw_metadata_policy_commit(&continuation));
  UnwindMetadataPolicyToken nestedTerminal{};
  assert(__unw_metadata_policy_enter(&nestedTerminal));
  assert(__unw_metadata_policy_current() == UNW_METADATA_TERMINAL);
  assert(__unw_metadata_policy_leave(&nestedTerminal));
  assert(__unw_metadata_policy_current() == UNW_METADATA_TERMINAL);
  if (!setjmp(resumed)) {
    // Placement storage avoids longjmp across a C++ automatic destructor.
    auto *landing = new (reinterpret_cast<Cursor *>(landingStorage))
        Cursor(&context, as, true);
    landing->setInfoBasedOnIPRegister();
    landing->jumpto();
    assert(false);
  }
  reinterpret_cast<Cursor *>(landingStorage)->~Cursor();
  assert(__unw_metadata_policy_current() == UNW_METADATA_TERMINAL);
  {
    Cursor seeded(&context, as, true);
    seeded.setInfoBasedOnIPRegister();
    seeded.getInfo(&info);
    assert(info.start_ip == 0x1000);
  }
  assert(__unw_metadata_policy_leave(&continuation));
  assert(__unw_metadata_policy_current() == UNW_METADATA_ORDINARY);

  // Generation exhaustion is a pre-admission error, never reuse an old token.
  uintptr_t generation = state.next;
  state.next = UINTPTR_MAX;
  UnwindMetadataPolicyToken exhausted{};
  assert(!__unw_metadata_policy_enter(&exhausted));
  assert(exhausted.generation == 0 && state.active == 0);
  assert(__unw_metadata_policy_current() == UNW_METADATA_ORDINARY);
  state.next = generation;
}
