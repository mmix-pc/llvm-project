//===-- Tests for MMIX AIO public input snapshots -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/linux/mmix/aio_input.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <sys/mman.h>
#include <unistd.h>

using LIBC_NAMESPACE::mmix::AioInput;
using LIBC_NAMESPACE::mmix::AioSyncInput;
using LIBC_NAMESPACE::mmix::normalize_aio_transfer_size;
using LIBC_NAMESPACE::mmix::snapshot_aio_event;
using LIBC_NAMESPACE::mmix::snapshot_aio_input;
using LIBC_NAMESPACE::mmix::snapshot_aio_sync_input;

namespace {

struct Allocator {
  alignas(AioInput) unsigned char memory[4 * sizeof(AioInput)];
  size_t attempts = 0;
  size_t freed = 0;
  size_t bytes = 0;
  bool fail = false;
  bool live = false;

  void *allocate(size_t size, size_t alignment) {
    ++attempts;
    bytes = size;
    EXPECT_FALSE(live);
    EXPECT_EQ(alignment, alignof(AioInput));
    EXPECT_EQ(size % alignment, size_t(0));
    if (fail || size > sizeof(memory))
      return nullptr;
    live = true;
    return memory;
  }
  void deallocate(void *ptr) {
    EXPECT_EQ(ptr, static_cast<void *>(memory));
    EXPECT_TRUE(live);
    live = false;
    ++freed;
  }
};

using List = LIBC_NAMESPACE::mmix::AioListInput<Allocator>;
static_assert(!__is_constructible(List, const List &));
static_assert(!__is_constructible(List, List &&));
static_assert(__is_same(decltype(AioInput::nbytes), size_t));
static_assert(__is_same(decltype(AioInput::offset), off_t));
static_assert(__is_same(decltype(AioInput::buffer), volatile void *));

// Native overlay tests use real inaccessible pages, not merely invalid pointer
// values. A prefix object has only its discriminant initialized; reading a
// trailing I/O/event field faults even without a sanitizer.
class GuardedPages {
  size_t page_size = 0;
  unsigned char *memory = nullptr;
  bool protected_page = false;

public:
  GuardedPages() {
    long size = ::sysconf(_SC_PAGESIZE);
    if (size <= 0)
      return;
    page_size = size_t(size);
    void *ptr = ::mmap(nullptr, 2 * page_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED)
      return;
    memory = static_cast<unsigned char *>(ptr);
    protected_page = ::mprotect(memory + page_size, page_size, PROT_NONE) == 0;
  }
  ~GuardedPages() {
    if (memory)
      EXPECT_EQ(::munmap(memory, 2 * page_size), 0);
  }
  bool valid() const { return protected_page; }
  void *unreadable() const { return memory + page_size; }

  template <class T> T *prefix(size_t bytes) {
    size_t aligned = (bytes + alignof(T) - 1) / alignof(T) * alignof(T);
    return new (memory + page_size - aligned) T;
  }
};

void callback(sigval) {}

aiocb transfer(int opcode) {
  aiocb cb{};
  cb.aio_fildes = 23;
  cb.aio_lio_opcode = opcode;
  cb.aio_sigevent.sigev_notify = SIGEV_NONE;
  return cb;
}

} // namespace

TEST(LlvmLibcAioInputTest, TransferSizeKeepsSingleTransferSemantics) {
  // No I/O or enormous allocation is needed to prove the integer mapping.
  constexpr size_t mmix_limit = size_t(INT32_MAX) & ~size_t(8191);
  const size_t counts[] = {0,
                           1,
                           mmix_limit - 1,
                           mmix_limit,
                           mmix_limit + 1,
                           size_t(INT32_MAX) - 1,
                           size_t(INT32_MAX),
                           size_t(SSIZE_MAX)};
  for (size_t count : counts) {
    uint32_t size = UINT32_MAX;
    errno = EDOM;
    ASSERT_EQ(normalize_aio_transfer_size(count, size), 0);
    EXPECT_EQ(size, count > size_t(INT32_MAX) ? uint32_t(INT32_MAX)
                                              : uint32_t(count));
    // The subsequent kernel cap must be equivalent to capping the original
    // public count, including values around MMIX's 8192-byte page boundary.
    EXPECT_EQ(size_t(size) < mmix_limit ? size_t(size) : mmix_limit,
              count < mmix_limit ? count : mmix_limit);
    EXPECT_EQ(errno, EDOM);
  }
  if constexpr (sizeof(size_t) > sizeof(uint32_t)) {
    const size_t large[] = {size_t(INT32_MAX) + 1, size_t(UINT32_MAX) - 1,
                            size_t(UINT32_MAX), size_t(UINT32_MAX) + 1};
    for (size_t count : large) {
      uint32_t size = 0;
      ASSERT_EQ(normalize_aio_transfer_size(count, size), 0);
      EXPECT_EQ(size, uint32_t(INT32_MAX));
    }
  }
}

TEST(LlvmLibcAioInputTest, UnrepresentableResultSizeDoesNotPublishOutput) {
  const size_t counts[] = {size_t(SSIZE_MAX) + 1, SIZE_MAX};
  for (size_t count : counts) {
    uint32_t size = 73;
    errno = EDOM;
    EXPECT_EQ(normalize_aio_transfer_size(count, size), EINVAL);
    EXPECT_EQ(size, uint32_t(73));
    EXPECT_EQ(errno, EDOM);
  }
}

TEST(LlvmLibcAioInputTest, SyncOperationValidationPrecedesControlBlockReads) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  AioSyncInput output;
  output.descriptor = 73;
  auto *cb = static_cast<aiocb *>(pages.unreadable());
  errno = EDOM;
  EXPECT_EQ(snapshot_aio_sync_input(0, cb, output), EINVAL);
  EXPECT_EQ(snapshot_aio_sync_input(-1, cb, output), EINVAL);
  EXPECT_EQ(snapshot_aio_sync_input(O_SYNC, nullptr, output), EINVAL);
  EXPECT_EQ(output.descriptor, 73);
  EXPECT_EQ(output.control_block, nullptr);
  EXPECT_EQ(errno, EDOM);
}

TEST(LlvmLibcAioInputTest, SyncIgnoresTransferFieldsAndSnapshotsNotification) {
  // All other fields are intentionally indeterminate; memory instrumentation
  // must not observe a whole-struct copy or transfer/list validation.
  aiocb cb;
  cb.aio_fildes = -23;
  cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
  cb.aio_sigevent.sigev_signo = 15;
  cb.aio_sigevent.sigev_value.sival_int = 37;
  AioSyncInput output;
  const int operations[] = {O_SYNC, O_DSYNC};
  for (int operation : operations) {
    errno = EDOM;
    ASSERT_EQ(snapshot_aio_sync_input(operation, &cb, output), 0);
    EXPECT_TRUE(output.control_block == &cb);
    EXPECT_EQ(output.descriptor, -23);
    EXPECT_EQ(output.data_only, operation == O_DSYNC);
    EXPECT_EQ(output.event.sigev_notify, int(SIGEV_SIGNAL));
    EXPECT_EQ(output.event.sigev_signo, 15);
    EXPECT_EQ(output.event.sigev_value.sival_int, 37);
    EXPECT_EQ(errno, EDOM);
  }
  cb.aio_fildes = 19;
  cb.aio_sigevent.sigev_value.sival_int = 41;
  EXPECT_EQ(output.descriptor, -23);
  EXPECT_EQ(output.event.sigev_value.sival_int, 37);
}

TEST(LlvmLibcAioInputTest, WholeListValidationBeforeReadingOrAllocating) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  auto *list = static_cast<aiocb **>(pages.unreadable());
  auto *sig = static_cast<sigevent *>(pages.unreadable());
  Allocator allocator;
  List input(allocator);
  errno = EDOM;
  EXPECT_EQ(input.capture(-1, list, INT_MAX, sig), EINVAL);
  EXPECT_EQ(input.capture(LIO_NOWAIT, list, -1, sig), EINVAL);
  EXPECT_EQ(input.capture(LIO_WAIT, list, INT_MIN, sig), EINVAL);
  EXPECT_EQ(input.capture(LIO_NOWAIT, nullptr, 1, sig), EINVAL);
  EXPECT_EQ(allocator.attempts, size_t(0));
  EXPECT_FALSE(input.ready());
  EXPECT_EQ(input.size(), size_t(0));
  EXPECT_EQ(input.data(), nullptr);
  EXPECT_EQ(errno, EDOM);
}

TEST(LlvmLibcAioInputTest, NullAndNopEntriesReadNoIoOrEventFields) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  auto *nop =
      pages.prefix<aiocb>(offsetof(aiocb, aio_lio_opcode) + sizeof(int));
  nop->aio_lio_opcode = LIO_NOP;
  aiocb *list[] = {nullptr, nop, nullptr, nop};
  Allocator allocator;
  List input(allocator);
  ASSERT_EQ(input.capture(LIO_NOWAIT, list, 4, nullptr), 0);
  EXPECT_TRUE(input.ready());
  EXPECT_EQ(input.size(), size_t(0));
  EXPECT_EQ(input.data(), nullptr);
  EXPECT_EQ(allocator.attempts, size_t(0));
  EXPECT_EQ(input.notification().sigev_notify, int(SIGEV_NONE));
  EXPECT_EQ(snapshot_aio_input(nullptr, LIO_READ).control_block, nullptr);
  EXPECT_EQ(snapshot_aio_input(nop, LIO_NOP).control_block, nullptr);
}

TEST(LlvmLibcAioInputTest, MalformedOpcodeRetainsOnlyKeyOpcodeAndMemberError) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  auto *bad =
      pages.prefix<aiocb>(offsetof(aiocb, aio_lio_opcode) + sizeof(int));
  bad->aio_lio_opcode = INT_MIN;
  aiocb good = transfer(LIO_WRITE);
  aiocb *list[] = {&good, nullptr, bad};
  Allocator allocator;
  List input(allocator);
  ASSERT_EQ(input.capture(LIO_WAIT, list, 3, nullptr), 0);
  ASSERT_EQ(input.size(), size_t(2));
  EXPECT_EQ(input.data()[0].member_error, 0);
  const auto &member = input.data()[1];
  EXPECT_TRUE(member.control_block == bad);
  EXPECT_EQ(member.opcode, INT_MIN);
  EXPECT_EQ(member.member_error, EINVAL);
  EXPECT_EQ(member.descriptor, -1);
  EXPECT_TRUE(member.buffer == nullptr);
  EXPECT_EQ(member.nbytes, size_t(0));
  EXPECT_EQ(member.offset, off_t(0));
  EXPECT_EQ(member.event.sigev_notify, int(SIGEV_NONE));
}

TEST(LlvmLibcAioInputTest, DuplicateRealMembersFailBeforeIoReadsOrAllocation) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  auto *cb = pages.prefix<aiocb>(offsetof(aiocb, aio_lio_opcode) + sizeof(int));
  aiocb *list[] = {cb, nullptr, cb};
  int opcodes[] = {LIO_READ, LIO_WRITE, INT_MAX};
  Allocator allocator;
  List input(allocator);
  for (int opcode : opcodes) {
    cb->aio_lio_opcode = opcode;
    EXPECT_EQ(input.capture(LIO_NOWAIT, list, 3,
                            static_cast<sigevent *>(pages.unreadable())),
              EINVAL);
    EXPECT_FALSE(input.ready());
    EXPECT_EQ(input.data(), nullptr);
  }
  EXPECT_EQ(allocator.attempts, size_t(0));
}

TEST(LlvmLibcAioInputTest, AllocationFailureBeforeTransferOrNotificationReads) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  auto *cb = pages.prefix<aiocb>(offsetof(aiocb, aio_lio_opcode) + sizeof(int));
  cb->aio_lio_opcode = LIO_READ;
  aiocb *list[] = {nullptr, cb};
  Allocator allocator;
  allocator.fail = true;
  List input(allocator);
  EXPECT_EQ(input.capture(LIO_NOWAIT, list, 2,
                          static_cast<sigevent *>(pages.unreadable())),
            ENOMEM);
  EXPECT_EQ(allocator.attempts, size_t(1));
  EXPECT_EQ(allocator.bytes, sizeof(AioInput));
  EXPECT_EQ(allocator.freed, size_t(0));
  EXPECT_FALSE(input.ready());
  EXPECT_EQ(input.size(), size_t(0));
  EXPECT_EQ(input.data(), nullptr);
  allocator.fail = false;
  aiocb good = transfer(LIO_READ);
  list[1] = &good;
  EXPECT_EQ(input.capture(LIO_NOWAIT, list, 2, nullptr), 0);
  EXPECT_TRUE(input.ready());
}

TEST(LlvmLibcAioInputTest, CapturesOrderedIndependentPublicWidthInputs) {
  char read_buffer[1], write_buffer[1];
  aiocb read = transfer(LIO_READ), write = transfer(LIO_WRITE);
  read.aio_reqprio = INT_MIN;
  write.aio_reqprio = INT_MAX;
  read.aio_buf = read_buffer;
  write.aio_buf = write_buffer;
  read.aio_nbytes = SIZE_MAX;
  write.aio_nbytes = size_t(UINT32_MAX);
  read.aio_offset = off_t(-7);
  write.aio_offset = off_t(123456789);
  write.aio_fildes = -8;
  read.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
  read.aio_sigevent.sigev_signo = SIGUSR1;
  read.aio_sigevent.sigev_value.sival_int = 37;
  aiocb nop;
  nop.aio_lio_opcode = LIO_NOP;
  aiocb *list[] = {nullptr, &read, &nop, &write};
  Allocator allocator;
  {
    List input(allocator);
    ASSERT_EQ(input.capture(LIO_NOWAIT, list, 4, nullptr), 0);
    ASSERT_EQ(input.size(), size_t(2));
    EXPECT_EQ(allocator.bytes, 2 * sizeof(AioInput));
    read.aio_fildes = 99;
    read.aio_buf = nullptr;
    read.aio_nbytes = 0;
    read.aio_offset = 0;
    read.aio_lio_opcode = LIO_WRITE;
    read.aio_sigevent.sigev_signo = SIGUSR2;
    read.aio_sigevent.sigev_value.sival_int = 0;
    list[1] = nullptr;
    const auto &first = input.data()[0];
    const auto &second = input.data()[1];
    EXPECT_TRUE(first.control_block == &read);
    EXPECT_EQ(first.opcode, int(LIO_READ));
    EXPECT_EQ(first.descriptor, 23);
    EXPECT_TRUE(first.buffer == read_buffer);
    EXPECT_EQ(first.nbytes, SIZE_MAX);
    EXPECT_EQ(first.offset, off_t(-7));
    EXPECT_EQ(first.event.sigev_signo, SIGUSR1);
    EXPECT_EQ(first.event.sigev_value.sival_int, 37);
    EXPECT_EQ(first.member_error, 0);
    EXPECT_TRUE(second.control_block == &write);
    EXPECT_EQ(second.opcode, int(LIO_WRITE));
    EXPECT_EQ(second.descriptor, -8);
    EXPECT_TRUE(second.buffer == write_buffer);
    EXPECT_EQ(second.nbytes, size_t(UINT32_MAX));
    EXPECT_EQ(second.offset, off_t(123456789));
    EXPECT_EQ(second.member_error, 0);
    EXPECT_EQ(input.mode(), int(LIO_NOWAIT));
    EXPECT_EQ(input.notification().sigev_notify, int(SIGEV_NONE));
    EXPECT_EQ(input.capture(LIO_WAIT, nullptr, 0, nullptr), EBUSY);
    EXPECT_EQ(input.size(), size_t(2));
  }
  EXPECT_FALSE(allocator.live);
  EXPECT_EQ(allocator.freed, size_t(1));
}

TEST(LlvmLibcAioInputTest, DirectReadWriteIgnoreListOpcodeAndPriority) {
  aiocb cb = transfer(INT_MIN);
  cb.aio_reqprio = INT_MAX;
  auto read = snapshot_aio_input(&cb, LIO_READ);
  auto write = snapshot_aio_input(&cb, LIO_WRITE);
  EXPECT_EQ(read.opcode, int(LIO_READ));
  EXPECT_EQ(write.opcode, int(LIO_WRITE));
  EXPECT_EQ(read.member_error, 0);
  EXPECT_EQ(write.member_error, 0);
  EXPECT_TRUE(read.control_block == &cb);
  EXPECT_EQ(write.descriptor, cb.aio_fildes);
  EXPECT_EQ(cb.aio_lio_opcode, INT_MIN);
  EXPECT_EQ(cb.aio_reqprio, INT_MAX);
}

TEST(LlvmLibcAioInputTest, TransferDoesNotCopyPrivateTailStorage) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  // MMIX's __request follows aio_offset. Native overlay layouts differ, but
  // guarding their tail also catches an accidental whole-aiocb copy.
  auto *cb = pages.prefix<aiocb>(offsetof(aiocb, aio_offset) + sizeof(off_t));
  cb->aio_fildes = 17;
  cb->aio_lio_opcode = LIO_WRITE;
  cb->aio_buf = nullptr;
  cb->aio_nbytes = 0;
  cb->aio_offset = -1;
  cb->aio_sigevent.sigev_notify = SIGEV_NONE;
  aiocb *list[] = {cb};
  Allocator allocator;
  List input(allocator);
  ASSERT_EQ(input.capture(LIO_WAIT, list, 1, nullptr), 0);
  EXPECT_TRUE(input.data()[0].control_block == cb);
  EXPECT_EQ(input.data()[0].descriptor, 17);
  EXPECT_EQ(input.data()[0].offset, off_t(-1));
  EXPECT_EQ(input.data()[0].member_error, 0);
}

TEST(LlvmLibcAioInputTest, SparseListHasNoRingSizedLimitOrAllocation) {
  aiocb cb = transfer(LIO_READ);
  aiocb *list[257]{};
  list[256] = &cb;
  Allocator allocator;
  List input(allocator);
  ASSERT_EQ(input.capture(LIO_NOWAIT, list, 257, nullptr), 0);
  EXPECT_EQ(input.size(), size_t(1));
  EXPECT_EQ(allocator.bytes, sizeof(AioInput));
  EXPECT_TRUE(input.data()[0].control_block == &cb);
}

TEST(LlvmLibcAioInputTest, WaitIgnoresGroupPointerButKeepsMemberNotification) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  aiocb cb = transfer(LIO_READ);
  cb.aio_sigevent.sigev_notify = SIGEV_SIGNAL;
  cb.aio_sigevent.sigev_signo = SIGUSR1;
  aiocb *list[] = {&cb};
  Allocator allocator;
  List input(allocator);
  ASSERT_EQ(input.capture(LIO_WAIT, list, 1,
                          static_cast<sigevent *>(pages.unreadable())),
            0);
  EXPECT_EQ(input.mode(), int(LIO_WAIT));
  EXPECT_EQ(input.notification().sigev_notify, int(SIGEV_NONE));
  EXPECT_EQ(input.data()[0].event.sigev_notify, int(SIGEV_SIGNAL));
  EXPECT_EQ(input.data()[0].event.sigev_signo, SIGUSR1);
}

TEST(LlvmLibcAioInputTest, EmptyListIgnoresListAndWaitSignalPointers) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  Allocator allocator;
  List input(allocator);
  ASSERT_EQ(input.capture(LIO_WAIT, static_cast<aiocb **>(pages.unreadable()),
                          0, static_cast<sigevent *>(pages.unreadable())),
            0);
  EXPECT_TRUE(input.ready());
  EXPECT_EQ(input.size(), size_t(0));
  EXPECT_EQ(allocator.attempts, size_t(0));
  EXPECT_EQ(input.notification().sigev_notify, int(SIGEV_NONE));
}

TEST(LlvmLibcAioInputTest, EmptyNowaitListStillSnapshotsGroupEvent) {
  sigevent event{};
  event.sigev_notify = SIGEV_SIGNAL;
  event.sigev_signo = SIGUSR2;
  event.sigev_value.sival_int = 42;
  Allocator allocator;
  List input(allocator);
  ASSERT_EQ(input.capture(LIO_NOWAIT, nullptr, 0, &event), 0);
  event.sigev_signo = SIGUSR1;
  event.sigev_value.sival_int = 0;
  EXPECT_EQ(input.notification().sigev_notify, int(SIGEV_SIGNAL));
  EXPECT_EQ(input.notification().sigev_signo, SIGUSR2);
  EXPECT_EQ(input.notification().sigev_value.sival_int, 42);
  EXPECT_EQ(allocator.attempts, size_t(0));
}

TEST(LlvmLibcAioInputTest, NoneAndUnknownEventsDoNotReadTrailingFields) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  auto *event =
      pages.prefix<sigevent>(offsetof(sigevent, sigev_notify) + sizeof(int));
  int modes[] = {SIGEV_NONE, INT_MIN};
  for (int mode : modes) {
    event->sigev_notify = mode;
    auto saved = snapshot_aio_event(event);
    EXPECT_EQ(saved.sigev_notify, mode);
    EXPECT_EQ(saved.sigev_signo, 0);
    EXPECT_EQ(saved.sigev_value.sival_int, 0);
    EXPECT_TRUE(saved.sigev_notify_function == nullptr);
    EXPECT_EQ(saved.sigev_notify_attributes, nullptr);
    Allocator allocator;
    List input(allocator);
    ASSERT_EQ(input.capture(LIO_NOWAIT, nullptr, 0, event), 0);
    EXPECT_EQ(input.notification().sigev_notify, mode);
  }
}

TEST(LlvmLibcAioInputTest, SignalCopiesOnlySignalFields) {
  sigevent event;
  event.sigev_notify = SIGEV_SIGNAL;
  event.sigev_signo = -1;
  event.sigev_value.sival_int = 73;
  auto saved = snapshot_aio_event(&event);
  EXPECT_EQ(saved.sigev_notify, int(SIGEV_SIGNAL));
  EXPECT_EQ(saved.sigev_signo, -1);
  EXPECT_EQ(saved.sigev_value.sival_int, 73);
  EXPECT_TRUE(saved.sigev_notify_function == nullptr);
  EXPECT_EQ(saved.sigev_notify_attributes, nullptr);
}

TEST(LlvmLibcAioInputTest, ThreadAttributesAreBorrowedUntilSynchronousPrepare) {
  GuardedPages pages;
  ASSERT_TRUE(pages.valid());
  sigevent event;
  event.sigev_notify = SIGEV_THREAD;
  event.sigev_value.sival_ptr = pages.unreadable();
  event.sigev_notify_function = callback;
  event.sigev_notify_attributes =
      static_cast<pthread_attr_t *>(pages.unreadable());
  auto saved = snapshot_aio_event(&event);
  event.sigev_notify_function = nullptr;
  event.sigev_notify_attributes = nullptr;
  event.sigev_value.sival_ptr = nullptr;
  EXPECT_EQ(saved.sigev_notify, int(SIGEV_THREAD));
  EXPECT_EQ(saved.sigev_signo, 0);
  EXPECT_EQ(saved.sigev_value.sival_ptr, pages.unreadable());
  EXPECT_TRUE(saved.sigev_notify_function == callback);
  EXPECT_EQ(saved.sigev_notify_attributes,
            static_cast<pthread_attr_t *>(pages.unreadable()));
}
