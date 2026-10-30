//===-- Tests for the owning Linux io_uring transport ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/OSUtil/linux/io_uring.h"
#include "src/__support/libc_errno.h"
#include "test/UnitTest/Test.h"

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;

namespace {

constexpr int FD = 37;
constexpr __u32 SQ_COUNT = 4;
constexpr __u32 CQ_COUNT = 8;
constexpr uint64_t COOKIE = UINT64_C(0xfedcba9876543210);

// The fake supplies only syscall results and mapped bytes. All ownership,
// layout validation, publication and consumption use the production Ring.
struct State {
  io_uring_params params = {};
  alignas(io_uring_sqe) unsigned char sq[512] = {};
  alignas(io_uring_cqe) unsigned char cq[512] = {};
  io_uring_sqe sqes[SQ_COUNT] = {};
  bool single;
  int setup_calls = 0, flags_calls = 0, map_calls = 0;
  int unmap_calls = 0, close_calls = 0, enter_calls = 0;
  int setup_error = 0, flags_error = 0, fd_flags = FD_CLOEXEC;
  int map_failure = 0, unmap_failure = 0, close_error = 0;
  int invalid_mapping = 0;
  int enter_error = 0;
  unsigned enter_result = 0, consume = 0;
  const io_uring_cqe *completion = nullptr;
  size_t sizes[3] = {};
  off_t offsets[3] = {};
  void *mapped[3] = {};
  void *unmapped[4] = {};
  size_t unmapped_sizes[4] = {};
  unsigned submitted[8] = {}, waited[8] = {}, entered_flags[8] = {};

  explicit State(bool single = true) : single(single) {
    params.sq_entries = SQ_COUNT;
    params.cq_entries = CQ_COUNT;
    params.features = IORING_FEAT_NODROP;
    if (single)
      params.features |= IORING_FEAT_SINGLE_MMAP;
    params.sq_off.head = 0;
    params.sq_off.tail = 4;
    params.sq_off.ring_mask = 8;
    params.sq_off.ring_entries = 12;
    params.sq_off.flags = 16;
    params.sq_off.dropped = 20;
    params.sq_off.array = 256;
    params.cq_off.head = 32;
    params.cq_off.tail = 36;
    params.cq_off.ring_mask = 40;
    params.cq_off.ring_entries = 44;
    params.cq_off.overflow = 48;
    params.cq_off.cqes = 128;
    sq_word(params.sq_off.ring_mask) = SQ_COUNT - 1;
    sq_word(params.sq_off.ring_entries) = SQ_COUNT;
    cq_word(params.cq_off.ring_mask) = CQ_COUNT - 1;
    cq_word(params.cq_off.ring_entries) = CQ_COUNT;
  }

  __u32 &sq_word(size_t offset) {
    return *reinterpret_cast<__u32 *>(sq + offset);
  }
  __u32 &cq_word(size_t offset) {
    return *reinterpret_cast<__u32 *>((single ? sq : cq) + offset);
  }
  io_uring_cqe &cqe(unsigned slot) {
    return reinterpret_cast<io_uring_cqe *>((single ? sq : cq) +
                                            params.cq_off.cqes)[slot];
  }
  void post(const io_uring_cqe &entry) {
    __u32 &tail = cq_word(params.cq_off.tail);
    cqe(tail & (CQ_COUNT - 1)) = entry;
    ++tail;
  }
};

struct FakeOperations {
  State *state;

  ErrorOr<int> setup(unsigned entries, io_uring_params &params) {
    ++state->setup_calls;
    EXPECT_GT(entries, 0U);
    const auto *bytes = reinterpret_cast<const unsigned char *>(&params);
    for (size_t i = 0; i < sizeof(params); ++i)
      EXPECT_EQ(bytes[i], static_cast<unsigned char>(0));
    if (state->setup_error)
      return Error(state->setup_error);
    params = state->params;
    return FD;
  }
  ErrorOr<int> descriptor_flags(int fd) {
    EXPECT_EQ(fd, FD);
    ++state->flags_calls;
    if (state->flags_error)
      return Error(state->flags_error);
    return state->fd_flags;
  }
  ErrorOr<void *> map(int fd, size_t size, off_t offset) {
    EXPECT_EQ(fd, FD);
    int index = state->map_calls++;
    EXPECT_LT(index, 3);
    if (index >= 3)
      return Error(EINVAL);
    state->sizes[index] = size;
    state->offsets[index] = offset;
    if (state->map_calls == state->map_failure)
      return Error(ENOMEM);
    void *address;
    if (offset == IORING_OFF_SQ_RING) {
      EXPECT_LE(size, sizeof(state->sq));
      address = state->sq;
    } else if (offset == IORING_OFF_CQ_RING) {
      EXPECT_FALSE(state->single);
      EXPECT_LE(size, sizeof(state->cq));
      address = state->cq;
    } else {
      EXPECT_EQ(offset, off_t(IORING_OFF_SQES));
      EXPECT_EQ(size, sizeof(state->sqes));
      address = state->sqes;
    }
    if (state->invalid_mapping == 1)
      address = nullptr;
    if (state->invalid_mapping == 2)
      address = state->sq + 1;
    if (state->invalid_mapping == 3)
      address = reinterpret_cast<void *>(UINTPTR_MAX & ~uintptr_t(7));
    state->mapped[index] = address;
    return address;
  }
  ErrorOr<int> unmap(void *address, size_t size) {
    int index = state->unmap_calls++;
    EXPECT_LT(index, 4);
    if (index < 4) {
      state->unmapped[index] = address;
      state->unmapped_sizes[index] = size;
    }
    if (state->unmap_calls == state->unmap_failure)
      return Error(EIO);
    return 0;
  }
  ErrorOr<int> close(int fd) {
    EXPECT_EQ(fd, FD);
    ++state->close_calls;
    if (state->close_error)
      return Error(state->close_error);
    return 0;
  }
  ErrorOr<unsigned> enter(int fd, unsigned submit, unsigned wait,
                          unsigned flags) {
    EXPECT_EQ(fd, FD);
    int index = state->enter_calls++;
    EXPECT_LT(index, 8);
    if (index < 8) {
      state->submitted[index] = submit;
      state->waited[index] = wait;
      state->entered_flags[index] = flags;
    }
    state->sq_word(state->params.sq_off.head) += state->consume;
    if (state->completion) {
      state->post(*state->completion);
      state->sq_word(state->params.sq_off.flags) &= ~IORING_SQ_CQ_OVERFLOW;
    }
    if (state->enter_error)
      return Error(state->enter_error);
    return state->enter_result;
  }
};

using Ring = LIBC_NAMESPACE::io_uring::Ring<FakeOperations>;

// Error-path calls also check that the output is untouched on failure.
ErrorOr<void> poll(Ring &ring) {
  io_uring_cqe completion = {COOKIE, -123, 456};
  auto result = ring.poll(completion);
  if (!result) {
    EXPECT_EQ(completion.user_data, __u64(COOKIE));
    EXPECT_EQ(completion.res, -123);
    EXPECT_EQ(completion.flags, 456U);
  }
  return result;
}

ErrorOr<void> wait(Ring &ring) {
  io_uring_cqe completion = {COOKIE, -123, 456};
  auto result = ring.wait(completion);
  if (!result) {
    EXPECT_EQ(completion.user_data, __u64(COOKIE));
    EXPECT_EQ(completion.res, -123);
    EXPECT_EQ(completion.flags, 456U);
  }
  return result;
}

template <class T> void expect_error(const ErrorOr<T> &result, int error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

io_uring_sqe request() {
  io_uring_sqe entry = {};
  entry.opcode = IORING_OP_READ;
  entry.flags = IOSQE_ASYNC;
  entry.fd = 19;
  entry.off = UINT64_C(0x123456789abcdef0);
  entry.addr = UINT64_C(0x87654321);
  entry.len = 123;
  entry.user_data = COOKIE;
  return entry;
}

void expect_cleanup(const State &state, int mappings) {
  EXPECT_EQ(state.unmap_calls, mappings);
  EXPECT_EQ(state.close_calls, 1);
  for (int i = 0; i < mappings; ++i) {
    EXPECT_EQ(state.unmapped[i], state.mapped[mappings - i - 1]);
    EXPECT_EQ(state.unmapped_sizes[i], state.sizes[mappings - i - 1]);
  }
}

} // namespace

TEST(LlvmLibcIoUringTest, ClosedAndZeroEntries) {
  State state;
  {
    Ring ring(FakeOperations{&state});
    expect_error(ring.enqueue(request()), EBADF);
    expect_error(ring.submit(), EBADF);
    expect_error(poll(ring), EBADF);
    expect_error(wait(ring), EBADF);
    expect_error(ring.initialize(0), EINVAL);
    EXPECT_TRUE(ring.reset().has_value());
  }
  EXPECT_EQ(state.setup_calls, 0);
  EXPECT_EQ(state.enter_calls, 0);
  EXPECT_EQ(state.unmap_calls, 0);
  EXPECT_EQ(state.close_calls, 0);
}

TEST(LlvmLibcIoUringTest, OwnsSingleAndSeparateMappings) {
  const bool modes[] = {false, true};
  for (bool single : modes) {
    State state(single);
    {
      Ring ring(FakeOperations{&state});
      // Non-power-of-two requests are rounded by the kernel, not rejected.
      ASSERT_TRUE(ring.initialize(3).has_value());
      expect_error(ring.initialize(4), EBUSY);
      EXPECT_EQ(state.setup_calls, 1);
      EXPECT_EQ(state.flags_calls, 1);
      EXPECT_EQ(state.map_calls, single ? 2 : 3);
      EXPECT_EQ(state.offsets[0], off_t(IORING_OFF_SQ_RING));
      EXPECT_EQ(state.sizes[0], size_t(272));
      if (!single) {
        EXPECT_EQ(state.offsets[1], off_t(IORING_OFF_CQ_RING));
        EXPECT_EQ(state.sizes[1], size_t(256));
      }
      EXPECT_EQ(state.offsets[state.map_calls - 1], off_t(IORING_OFF_SQES));
      EXPECT_EQ(state.sizes[state.map_calls - 1], sizeof(state.sqes));
    }
    expect_cleanup(state, single ? 2 : 3);
  }
}

TEST(LlvmLibcIoUringTest, SingleMappingUsesLargerCqExtent) {
  State state;
  state.params.cq_off.cqes = 320;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  EXPECT_EQ(state.sizes[0], size_t(448));
  EXPECT_TRUE(ring.reset().has_value());
  expect_cleanup(state, 2);
}

TEST(LlvmLibcIoUringTest, SetupErrorsHaveNoFallback) {
  const int errors[] = {ENOSYS, EPERM, ENOMEM, EINTR};
  for (int error : errors) {
    State state;
    state.setup_error = error;
    {
      Ring ring(FakeOperations{&state});
      expect_error(ring.initialize(4), error);
    }
    EXPECT_EQ(state.setup_calls, 1);
    EXPECT_EQ(state.flags_calls, 0);
    EXPECT_EQ(state.map_calls, 0);
    EXPECT_EQ(state.close_calls, 0);
  }
}

TEST(LlvmLibcIoUringTest, AuditsAtomicCloseOnExec) {
  const int errors[] = {0, EBADF, EINTR};
  for (int error : errors) {
    State state;
    state.flags_error = error;
    state.fd_flags = 0;
    {
      Ring ring(FakeOperations{&state});
      expect_error(ring.initialize(4), error ? error : EPROTO);
    }
    EXPECT_EQ(state.flags_calls, 1);
    EXPECT_EQ(state.map_calls, 0);
    expect_cleanup(state, 0);
  }
}

TEST(LlvmLibcIoUringTest, EveryPartialMappingFailureCleansUp) {
  const bool modes[] = {false, true};
  for (bool single : modes) {
    for (int failure = 1; failure <= (single ? 2 : 3); ++failure) {
      State state(single);
      state.map_failure = failure;
      {
        Ring ring(FakeOperations{&state});
        expect_error(ring.initialize(4), ENOMEM);
        expect_error(poll(ring), EBADF);
        EXPECT_TRUE(ring.reset().has_value());
      }
      EXPECT_EQ(state.map_calls, failure);
      expect_cleanup(state, failure - 1);
    }
  }
}

TEST(LlvmLibcIoUringTest, InvalidMappingAddressesAreUnmappedWithoutAccess) {
  for (int invalid = 1; invalid <= 3; ++invalid) {
    State state;
    state.invalid_mapping = invalid;
    {
      Ring ring(FakeOperations{&state});
      expect_error(ring.initialize(4), EPROTO);
    }
    EXPECT_EQ(state.map_calls, 1);
    expect_cleanup(state, 1);
  }
}

TEST(LlvmLibcIoUringTest, CleanupAttemptsAllResourcesAndNeverRetriesClose) {
  const bool modes[] = {false, true};
  for (bool single : modes) {
    int mappings = single ? 2 : 3;
    for (int failure = 1; failure <= mappings; ++failure) {
      State state(single);
      {
        Ring ring(FakeOperations{&state});
        ASSERT_TRUE(ring.initialize(4).has_value());
        state.unmap_failure = failure;
        state.close_error = EINTR;
        expect_error(ring.reset(), EIO);
        expect_cleanup(state, mappings);
        expect_error(ring.submit(), EBADF);
        expect_error(ring.initialize(4), EBUSY);
        EXPECT_EQ(state.setup_calls, 1);
        EXPECT_EQ(state.unmap_calls, mappings);
        EXPECT_TRUE(ring.reset().has_value());
        EXPECT_EQ(state.unmap_calls, mappings + 1);
        EXPECT_EQ(state.unmapped[mappings], state.unmapped[failure - 1]);
        EXPECT_EQ(state.unmapped_sizes[mappings],
                  state.unmapped_sizes[failure - 1]);
        EXPECT_EQ(state.close_calls, 1);
        EXPECT_TRUE(ring.reset().has_value());
      }
      EXPECT_EQ(state.unmap_calls, mappings + 1);
      EXPECT_EQ(state.close_calls, 1);
    }
  }

  State close_state;
  {
    Ring ring(FakeOperations{&close_state});
    ASSERT_TRUE(ring.initialize(4).has_value());
    close_state.close_error = EINTR;
    expect_error(ring.reset(), EINTR);
  }
  expect_cleanup(close_state, 2);
}

TEST(LlvmLibcIoUringTest, RollbackPreservesOriginalErrorAndAllowsReinitialize) {
  State state;
  Ring ring(FakeOperations{&state});
  state.map_failure = 2;
  state.unmap_failure = 1;
  state.close_error = EINTR;
  expect_error(ring.initialize(4), ENOMEM);
  expect_cleanup(state, 1);
  expect_error(ring.initialize(4), EBUSY);
  EXPECT_EQ(state.setup_calls, 1);
  EXPECT_EQ(state.map_calls, 2);
  EXPECT_EQ(state.unmap_calls, 1);
  EXPECT_TRUE(ring.reset().has_value());
  EXPECT_EQ(state.unmap_calls, 2);
  EXPECT_EQ(state.unmapped[1], state.unmapped[0]);
  EXPECT_EQ(state.unmapped_sizes[1], state.unmapped_sizes[0]);
  EXPECT_EQ(state.close_calls, 1);
  state = State();
  ASSERT_TRUE(ring.initialize(4).has_value());
  EXPECT_TRUE(ring.reset().has_value());
  expect_cleanup(state, 2);
}

TEST(LlvmLibcIoUringTest, DestructorRetriesOnlyRetainedMappings) {
  State state;
  {
    Ring ring(FakeOperations{&state});
    ASSERT_TRUE(ring.initialize(4).has_value());
    state.unmap_failure = 1;
    expect_error(ring.reset(), EIO);
    expect_cleanup(state, 2);
  }
  EXPECT_EQ(state.unmap_calls, 3);
  EXPECT_EQ(state.unmapped[2], state.unmapped[0]);
  EXPECT_EQ(state.unmapped_sizes[2], state.unmapped_sizes[0]);
  EXPECT_EQ(state.close_calls, 1);
}

TEST(LlvmLibcIoUringTest, DestructorTrapsOnUnreleasedMappings) {
  EXPECT_DEATH(
      [] {
        State state;
        Ring ring(FakeOperations{&state});
        if (!ring.initialize(4))
          return;
        state.unmap_failure = 1;
        if (ring.reset())
          return;
        state.unmap_failure = state.unmap_calls + 1;
      },
      WITH_SIGNAL(-1));
}

TEST(LlvmLibcIoUringTest, RejectsInvalidReturnedCapacitiesAndModes) {
  for (int invalid = 0; invalid < 9; ++invalid) {
    State state;
    switch (invalid) {
    case 0:
      state.params.sq_entries = 0;
      break;
    case 1:
      state.params.sq_entries = 3;
      break;
    case 2:
      state.params.sq_entries = 2;
      break;
    case 3:
      state.params.cq_entries = 0;
      break;
    case 4:
      state.params.cq_entries = 7;
      break;
    case 5:
      state.params.cq_entries = 2;
      break;
    case 6:
      state.params.sq_entries = __u32(1) << 31;
      break;
    case 7:
      state.params.flags = IORING_SETUP_SQPOLL;
      break;
    case 8:
      state.params.flags = IORING_SETUP_IOPOLL;
      break;
    }
    {
      Ring ring(FakeOperations{&state});
      expect_error(ring.initialize(4), EPROTO);
    }
    EXPECT_EQ(state.map_calls, 0);
    expect_cleanup(state, 0);
  }
}

TEST(LlvmLibcIoUringTest, RejectsOutOfBoundsMisalignedAndOverlappingOffsets) {
  for (int invalid = 0; invalid < 8; ++invalid) {
    State state;
    switch (invalid) {
    case 0:
      state.params.sq_off.head = 272;
      break;
    case 1:
      state.params.cq_off.tail = 256;
      break;
    case 2:
      state.params.sq_off.head = 1;
      break;
    case 3:
      state.params.cq_off.cqes = 129;
      break;
    case 4:
      state.params.sq_off.tail = state.params.sq_off.head;
      break;
    case 5:
      state.params.sq_off.array = 16;
      break;
    case 6:
      state.params.cq_off.head = state.params.sq_off.head;
      break;
    case 7:
      state.params.sq_off.array = state.params.cq_off.cqes;
      break;
    }
    {
      Ring ring(FakeOperations{&state});
      expect_error(ring.initialize(4), EPROTO);
    }
    EXPECT_EQ(state.map_calls, 0);
    expect_cleanup(state, 0);
  }
}

TEST(LlvmLibcIoUringTest, ExtentArithmeticDoesNotWrap) {
  State state;
  // The sum exceeds 32 bits. On a 64-bit host the arithmetic is valid but this
  // deliberately misaligned CQE offset must still be rejected before mapping.
  state.params.cq_off.cqes = UINT32_MAX;
  Ring ring(FakeOperations{&state});
  expect_error(ring.initialize(4), sizeof(size_t) == 4 ? EOVERFLOW : EPROTO);
  EXPECT_EQ(state.map_calls, 0);
  expect_cleanup(state, 0);
}

TEST(LlvmLibcIoUringTest, RejectsCorruptMappedGeometryAndInitialIndices) {
  for (int invalid = 0; invalid < 10; ++invalid) {
    State state;
    int error = EPROTO;
    switch (invalid) {
    case 0:
      state.sq_word(state.params.sq_off.ring_mask) = 7;
      break;
    case 1:
      state.sq_word(state.params.sq_off.ring_entries) = 8;
      break;
    case 2:
      state.cq_word(state.params.cq_off.ring_mask) = UINT32_MAX;
      break;
    case 3:
      state.cq_word(state.params.cq_off.ring_entries) = 4;
      break;
    case 4:
      state.sq_word(state.params.sq_off.head) = 1;
      break;
    case 5:
      state.sq_word(state.params.sq_off.tail) = 1;
      break;
    case 6:
      state.cq_word(state.params.cq_off.head) = 1;
      break;
    case 7:
      state.cq_word(state.params.cq_off.tail) = 1;
      break;
    case 8:
      state.sq_word(state.params.sq_off.dropped) = 1;
      error = EIO;
      break;
    case 9:
      state.cq_word(state.params.cq_off.overflow) = 1;
      error = EOVERFLOW;
      break;
    }
    {
      Ring ring(FakeOperations{&state});
      expect_error(ring.initialize(4), error);
    }
    expect_cleanup(state, 2);
  }
}

TEST(LlvmLibcIoUringTest, EnqueueFullAndWrappedIndices) {
  State state;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  auto entry = request();
  for (__u32 i = 0; i < SQ_COUNT; ++i) {
    entry.user_data = COOKIE + i;
    ASSERT_TRUE(ring.enqueue(entry).has_value());
    EXPECT_EQ(state.sq_word(state.params.sq_off.tail), i + 1);
    EXPECT_EQ(state.sq_word(state.params.sq_off.array + i * sizeof(__u32)), i);
    const auto *actual =
        reinterpret_cast<const unsigned char *>(&state.sqes[i]);
    const auto *expected = reinterpret_cast<const unsigned char *>(&entry);
    for (size_t byte = 0; byte < sizeof(entry); ++byte)
      EXPECT_EQ(actual[byte], expected[byte]);
  }
  expect_error(ring.enqueue(request()), EAGAIN);
  EXPECT_EQ(state.sq_word(state.params.sq_off.tail), SQ_COUNT);
  EXPECT_EQ(state.sqes[0].user_data, __u64(COOKIE));
  EXPECT_EQ(state.enter_calls, 0);
  state.sq_word(state.params.sq_off.head) = UINT32_MAX;
  state.sq_word(state.params.sq_off.tail) = UINT32_MAX;
  ASSERT_TRUE(ring.enqueue(request()).has_value());
  EXPECT_EQ(state.sq_word(state.params.sq_off.tail), 0U);
  EXPECT_EQ(state.sqes[SQ_COUNT - 1].user_data, __u64(COOKIE));
  EXPECT_EQ(state.sq_word(state.params.sq_off.array + 3 * sizeof(__u32)), 3U);
}

TEST(LlvmLibcIoUringTest, RejectsCorruptLiveGeometryBeforeAccess) {
  State state;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  state.sq_word(state.params.sq_off.ring_mask) = UINT32_MAX;
  expect_error(ring.enqueue(request()), EPROTO);
  expect_error(ring.submit(), EPROTO);
  state.sq_word(state.params.sq_off.ring_mask) = SQ_COUNT - 1;
  state.sq_word(state.params.sq_off.tail) = SQ_COUNT + 1;
  expect_error(ring.enqueue(request()), EPROTO);
  expect_error(ring.submit(), EPROTO);
  EXPECT_EQ(state.sqes[0].user_data, __u64(0));
  EXPECT_EQ(state.sq_word(state.params.sq_off.tail), SQ_COUNT + 1);
  state.cq_word(state.params.cq_off.tail) = CQ_COUNT + 1;
  expect_error(poll(ring), EPROTO);
  expect_error(wait(ring), EPROTO);
  EXPECT_EQ(state.cq_word(state.params.cq_off.head), 0U);
  state.cq_word(state.params.cq_off.tail) = 1;
  state.cq_word(state.params.cq_off.ring_entries) = 2;
  expect_error(poll(ring), EPROTO);
  EXPECT_EQ(state.cq_word(state.params.cq_off.head), 0U);
  EXPECT_EQ(state.enter_calls, 0);
}

TEST(LlvmLibcIoUringTest, ShortSubmitAndErrorsRetainKernelProgress) {
  State state;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  auto empty = ring.submit();
  ASSERT_TRUE(empty.has_value());
  EXPECT_EQ(*empty, 0U);
  EXPECT_EQ(state.enter_calls, 0);
  for (unsigned i = 0; i < 3; ++i)
    ASSERT_TRUE(ring.enqueue(request()).has_value());
  state.enter_result = state.consume = 1;
  auto short_result = ring.submit();
  ASSERT_TRUE(short_result.has_value());
  EXPECT_EQ(*short_result, 1U);
  EXPECT_EQ(state.submitted[0], 3U);
  EXPECT_EQ(state.waited[0], 0U);
  EXPECT_EQ(state.entered_flags[0], 0U);
  state.enter_error = EINTR;
  expect_error(ring.submit(), EINTR);
  EXPECT_EQ(state.enter_calls, 2);
  EXPECT_EQ(state.submitted[1], 2U);
  EXPECT_EQ(state.sq_word(state.params.sq_off.tail), 3U);
  state.consume = 0;
  state.enter_error = EBUSY;
  expect_error(ring.submit(), EBUSY);
  EXPECT_EQ(state.submitted[2], 1U);
  EXPECT_EQ(state.enter_calls, 3);
  state.enter_error = 0;
  state.enter_result = 0;
  auto zero = ring.submit();
  ASSERT_TRUE(zero.has_value());
  EXPECT_EQ(*zero, 0U);
  EXPECT_EQ(state.submitted[3], 1U);
  EXPECT_EQ(state.sq_word(state.params.sq_off.tail), 3U);
}

TEST(LlvmLibcIoUringTest, PollCopiesOneCompletionIncludingNegativeResult) {
  State state;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  expect_error(poll(ring), EAGAIN);
  EXPECT_EQ(state.enter_calls, 0);
  state.post({COOKIE, -EBADF, IORING_CQE_F_MORE});
  state.post({COOKIE + 1, 7, 0});
  io_uring_cqe first = {};
  ASSERT_TRUE(ring.poll(first).has_value());
  EXPECT_EQ(first.user_data, __u64(COOKIE));
  EXPECT_EQ(first.res, -EBADF);
  EXPECT_EQ(first.flags, unsigned(IORING_CQE_F_MORE));
  EXPECT_EQ(state.cq_word(state.params.cq_off.head), 1U);
  io_uring_cqe second = {};
  ASSERT_TRUE(ring.wait(second).has_value());
  EXPECT_EQ(second.user_data, __u64(COOKIE + 1));
  EXPECT_EQ(second.res, 7);
  EXPECT_EQ(state.enter_calls, 0);
  expect_error(poll(ring), EAGAIN);
}

TEST(LlvmLibcIoUringTest, CompletionIndicesWrapAndFullCqIsReadable) {
  State state(false);
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  state.cq_word(state.params.cq_off.head) = UINT32_MAX;
  state.cq_word(state.params.cq_off.tail) = UINT32_MAX;
  for (unsigned i = 0; i < CQ_COUNT; ++i)
    state.post({COOKIE + i, int(i), 0});
  for (unsigned i = 0; i < CQ_COUNT; ++i) {
    io_uring_cqe result = {};
    ASSERT_TRUE(ring.poll(result).has_value());
    EXPECT_EQ(result.user_data, __u64(COOKIE + i));
    EXPECT_EQ(result.res, int(i));
  }
  EXPECT_EQ(state.cq_word(state.params.cq_off.head), CQ_COUNT - 1);
  expect_error(poll(ring), EAGAIN);
}

TEST(LlvmLibcIoUringTest, WaitUsesGeteventsWithoutSubmittingOrRetrying) {
  State state;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  ASSERT_TRUE(ring.enqueue(request()).has_value());
  state.enter_error = EINTR;
  expect_error(wait(ring), EINTR);
  EXPECT_EQ(state.enter_calls, 1);
  EXPECT_EQ(state.submitted[0], 0U);
  EXPECT_EQ(state.waited[0], 1U);
  EXPECT_EQ(state.entered_flags[0], unsigned(IORING_ENTER_GETEVENTS));
  EXPECT_EQ(state.sq_word(state.params.sq_off.tail), 1U);
  state.enter_error = 0;
  expect_error(wait(ring), EAGAIN);
  EXPECT_EQ(state.enter_calls, 2);
  io_uring_cqe completion = {COOKIE, 123, 0};
  state.completion = &completion;
  io_uring_cqe result = {};
  ASSERT_TRUE(ring.wait(result).has_value());
  EXPECT_EQ(result.user_data, __u64(COOKIE));
  EXPECT_EQ(result.res, 123);
  EXPECT_EQ(state.enter_calls, 3);
}

TEST(LlvmLibcIoUringTest, PollFlushesQueuedOverflowWithoutBlocking) {
  State state;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  state.sq_word(state.params.sq_off.flags) = IORING_SQ_CQ_OVERFLOW;
  state.post({COOKIE, 1, 0});
  ASSERT_TRUE(poll(ring).has_value());
  EXPECT_EQ(state.enter_calls, 0);
  state.enter_error = EINTR;
  expect_error(poll(ring), EINTR);
  EXPECT_EQ(state.enter_calls, 1);
  state.enter_error = 0;
  expect_error(poll(ring), EAGAIN);
  EXPECT_EQ(state.enter_calls, 2);
  io_uring_cqe completion = {COOKIE + 1, 2, 0};
  state.completion = &completion;
  io_uring_cqe result = {};
  ASSERT_TRUE(ring.poll(result).has_value());
  EXPECT_EQ(result.user_data, __u64(COOKIE + 1));
  EXPECT_EQ(state.enter_calls, 3);
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(state.submitted[i], 0U);
    EXPECT_EQ(state.waited[i], 0U);
    EXPECT_EQ(state.entered_flags[i], unsigned(IORING_ENTER_GETEVENTS));
  }
}

TEST(LlvmLibcIoUringTest, LostCompletionsAreNotReportedAsEmptyEvenWithNodrop) {
  const bool nodrop_modes[] = {false, true};
  for (bool nodrop : nodrop_modes) {
    State state;
    if (!nodrop)
      state.params.features &= ~IORING_FEAT_NODROP;
    Ring ring(FakeOperations{&state});
    ASSERT_TRUE(ring.initialize(4).has_value());
    state.cq_word(state.params.cq_off.overflow) = 1;
    expect_error(poll(ring), EOVERFLOW);
    expect_error(wait(ring), EOVERFLOW);
    expect_error(ring.submit(), EOVERFLOW);
    expect_error(ring.enqueue(request()), EOVERFLOW);
    state.post({COOKIE, 0, 0});
    expect_error(poll(ring), EOVERFLOW);
    EXPECT_EQ(state.cq_word(state.params.cq_off.head), 0U);
    EXPECT_EQ(state.enter_calls, 0);
  }
}

TEST(LlvmLibcIoUringTest, DroppedSubmissionsAndErrnoPreservation) {
  State state;
  Ring ring(FakeOperations{&state});
  ASSERT_TRUE(ring.initialize(4).has_value());
  state.sq_word(state.params.sq_off.dropped) = 1;
  int saved_errno = libc_errno;
  libc_errno = EDOM;
  auto enqueued = ring.enqueue(request());
  auto submitted = ring.submit();
  auto polled = poll(ring);
  auto waited = wait(ring);
  state.close_error = EINTR;
  auto closed = ring.reset();
  int observed_errno = libc_errno;
  libc_errno = saved_errno;
  expect_error(enqueued, EIO);
  expect_error(submitted, EIO);
  expect_error(polled, EIO);
  expect_error(waited, EIO);
  expect_error(closed, EINTR);
  EXPECT_EQ(observed_errno, EDOM);
  EXPECT_EQ(state.enter_calls, 0);
}
