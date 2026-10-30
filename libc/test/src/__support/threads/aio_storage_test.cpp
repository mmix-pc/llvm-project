//===-- Tests for private growable AIO record storage ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/func/aligned_alloc.h"
#include "hdr/func/free.h"
#include "src/__support/threads/aio_storage.h"
#include "test/UnitTest/Test.h"

namespace {

struct State {
  bool gate = false;
  bool fail = false;
  size_t attempts = 0;
  size_t allocated = 0;
  size_t freed = 0;
  size_t constructed = 0;
  size_t destroyed = 0;
};

struct Allocator {
  State &state;

  void *allocate(size_t size, size_t alignment) {
    EXPECT_FALSE(state.gate);
    EXPECT_EQ(size % alignment, size_t(0));
    ++state.attempts;
    if (state.fail)
      return nullptr;
    void *memory = ::aligned_alloc(alignment, size);
    if (memory)
      ++state.allocated;
    return memory;
  }

  void deallocate(void *memory) {
    EXPECT_FALSE(state.gate);
    ++state.freed;
    ::free(memory);
  }
};

struct Gate {
  State &state;
  explicit Gate(State &state) : state(state) {
    EXPECT_FALSE(state.gate);
    state.gate = true;
  }
  ~Gate() { state.gate = false; }
};

// Non-default-constructible, nonmovable and over-aligned, like an owner whose
// identity and external references must survive growth and unrelated release.
struct alignas(64) Record {
  State &state;
  const size_t identity;
  Record *const original_address;

  Record(State &state, size_t identity)
      : state(state), identity(identity), original_address(this) {
    EXPECT_FALSE(state.gate);
    ++state.constructed;
  }
  Record(const Record &) = delete;
  Record &operator=(const Record &) = delete;
  ~Record() {
    EXPECT_FALSE(state.gate);
    EXPECT_EQ(original_address, this);
    ++state.destroyed;
  }
};

template <size_t N>
using Storage = LIBC_NAMESPACE::AioStorage<Record, N, Allocator>;

static_assert(!__is_constructible(Storage<4>, const Storage<4> &));
static_assert(!__is_constructible(Storage<4>, Storage<4> &&));
static_assert(!__is_constructible(Storage<4>::Slot, const Storage<4>::Slot &));
static_assert(!__is_constructible(Storage<4>::Block,
                                  const Storage<4>::Block &));

template <size_t N> void drain(Storage<N> &storage, State &state) {
  for (;;) {
    typename Storage<N>::Block *block;
    {
      Gate gate(state);
      EXPECT_EQ(storage.size(), size_t(0));
      block = storage.detach_empty();
      if (!block) {
        EXPECT_EQ(storage.capacity(), size_t(0));
        break;
      }
    }
    EXPECT_TRUE(Storage<N>::dispose(block));
  }
  EXPECT_EQ(state.allocated, state.freed);
}

} // namespace

TEST(LlvmLibcAioStorageTest, EmptyPoolDoesNotAllocateAtTheGate) {
  State state;
  Storage<4> storage;
  {
    Gate gate(state);
    EXPECT_EQ(storage.capacity(), size_t(0));
    EXPECT_EQ(storage.size(), size_t(0));
    EXPECT_EQ(storage.acquire(), nullptr);
    EXPECT_EQ(storage.detach_empty(), nullptr);
  }
  EXPECT_EQ(state.attempts, size_t(0));
  EXPECT_FALSE(Storage<4>::dispose(nullptr));
}

TEST(LlvmLibcAioStorageTest, GrowthBeyond128PreservesEveryLiveAddress) {
  State state;
  Allocator allocator{state};
  Storage<8> storage;
  Storage<8>::Slot *slots[160]{};
  Record *records[160]{};
  for (size_t i = 0; i < 160; ++i) {
    if (i % 8 == 0) {
      auto *block = Storage<8>::prepare(allocator);
      ASSERT_NE(block, nullptr);
      Gate gate(state);
      ASSERT_TRUE(storage.publish(*block));
    }
    {
      Gate gate(state);
      slots[i] = storage.acquire();
      ASSERT_NE(slots[i], nullptr);
    }
    EXPECT_EQ(reinterpret_cast<uintptr_t>(slots[i]->storage()) %
                  alignof(Record),
              uintptr_t(0));
    records[i] = new (slots[i]->storage()) Record(state, i);
    for (size_t j = 0; j <= i; ++j) {
      EXPECT_EQ(records[j]->original_address, records[j]);
      EXPECT_EQ(records[j]->identity, j);
      EXPECT_EQ(static_cast<void *>(records[j]), slots[j]->storage());
    }
  }
  {
    Gate gate(state);
    EXPECT_EQ(storage.capacity(), size_t(160));
    EXPECT_EQ(storage.size(), size_t(160));
    EXPECT_EQ(storage.acquire(), nullptr);
    EXPECT_EQ(storage.detach_empty(), nullptr);
  }
  EXPECT_EQ(state.attempts, size_t(20));
  EXPECT_EQ(state.destroyed, size_t(0));
  // Releasing holes must not compact the remaining records.
  for (size_t parity = 0; parity < 2; ++parity) {
    for (size_t i = parity; i < 160; i += 2) {
      EXPECT_EQ(records[i]->identity, i);
      EXPECT_EQ(records[i]->original_address, records[i]);
      records[i]->~Record();
      Gate gate(state);
      ASSERT_TRUE(storage.release(*slots[i]));
    }
  }
  EXPECT_EQ(state.constructed, size_t(160));
  EXPECT_EQ(state.destroyed, size_t(160));
  drain(storage, state);
}

TEST(LlvmLibcAioStorageTest, AllocationFailurePreservesLeasedAndFreeSlots) {
  State state;
  Allocator allocator{state};
  Storage<2> storage;
  auto *block = Storage<2>::prepare(allocator);
  ASSERT_NE(block, nullptr);
  Storage<2>::Slot *first;
  {
    Gate gate(state);
    ASSERT_TRUE(storage.publish(*block));
    first = storage.acquire();
    ASSERT_NE(first, nullptr);
  }
  Record *record = new (first->storage()) Record(state, 73);
  state.fail = true;
  EXPECT_EQ(Storage<2>::prepare(allocator), nullptr);
  Storage<2>::Slot *second;
  {
    Gate gate(state);
    EXPECT_EQ(storage.capacity(), size_t(2));
    EXPECT_EQ(storage.size(), size_t(1));
    second = storage.acquire();
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(storage.acquire(), nullptr);
  }
  EXPECT_EQ(record->identity, size_t(73));
  EXPECT_EQ(record->original_address, record);
  EXPECT_EQ(state.allocated, size_t(1));
  EXPECT_EQ(state.freed, size_t(0));
  EXPECT_EQ(state.destroyed, size_t(0));
  record->~Record();
  {
    Gate gate(state);
    ASSERT_TRUE(storage.release(*first));
    // A lease abandoned before construction can also be returned.
    ASSERT_TRUE(storage.release(*second));
  }
  state.fail = false;
  auto *growth = Storage<2>::prepare(allocator);
  ASSERT_NE(growth, nullptr);
  {
    Gate gate(state);
    ASSERT_TRUE(storage.publish(*growth));
    EXPECT_EQ(storage.capacity(), size_t(4));
  }
  drain(storage, state);
}

TEST(LlvmLibcAioStorageTest, IndependentPreparationsMayBothPublishOrRollBack) {
  const bool reverse_orders[] = {false, true};
  for (bool reverse : reverse_orders) {
    State state;
    Allocator allocator{state};
    Storage<2> storage;
    // Model two preparers outside the gate, before either publication.
    auto *first = Storage<2>::prepare(allocator);
    auto *second = Storage<2>::prepare(allocator);
    auto *rollback = Storage<2>::prepare(allocator);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(rollback, nullptr);
    {
      Gate gate(state);
      EXPECT_EQ(storage.capacity(), size_t(0));
      ASSERT_TRUE(storage.publish(*(reverse ? second : first)));
    }
    ASSERT_TRUE(Storage<2>::dispose(rollback));
    {
      Gate gate(state);
      ASSERT_TRUE(storage.publish(*(reverse ? first : second)));
      EXPECT_FALSE(storage.publish(*first));
      EXPECT_FALSE(storage.publish(*second));
      EXPECT_EQ(storage.capacity(), size_t(4));
      EXPECT_EQ(storage.size(), size_t(0));
    }
    EXPECT_EQ(state.constructed, size_t(0));
    EXPECT_EQ(state.destroyed, size_t(0));
    drain(storage, state);
  }
}

TEST(LlvmLibcAioStorageTest, BusyBlockCannotDetachUntilEveryLeaseIsReturned) {
  State state;
  Allocator allocator{state};
  Storage<2> storage;
  auto *block = Storage<2>::prepare(allocator);
  ASSERT_NE(block, nullptr);
  Storage<2>::Slot *first;
  Storage<2>::Slot *second;
  {
    Gate gate(state);
    ASSERT_TRUE(storage.publish(*block));
    first = storage.acquire();
    second = storage.acquire();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(storage.detach_empty(), nullptr);
    ASSERT_TRUE(storage.release(*first));
    EXPECT_FALSE(storage.release(*first));
    EXPECT_EQ(storage.detach_empty(), nullptr);
    EXPECT_EQ(storage.size(), size_t(1));
    ASSERT_TRUE(storage.release(*second));
    ASSERT_EQ(storage.detach_empty(), block);
    EXPECT_EQ(storage.capacity(), size_t(0));
    EXPECT_EQ(storage.acquire(), nullptr);
    EXPECT_FALSE(storage.release(*first));
  }
  EXPECT_EQ(state.freed, size_t(0));
  ASSERT_TRUE(Storage<2>::dispose(block));
  EXPECT_EQ(state.allocated, state.freed);
}

TEST(LlvmLibcAioStorageTest, EmptyBlockCanDetachBehindBusyHeadAndRepublish) {
  State state;
  Allocator allocator{state};
  Storage<1> storage;
  auto *empty = Storage<1>::prepare(allocator);
  auto *busy = Storage<1>::prepare(allocator);
  ASSERT_NE(empty, nullptr);
  ASSERT_NE(busy, nullptr);
  Storage<1>::Slot *slot;
  {
    Gate gate(state);
    ASSERT_TRUE(storage.publish(*empty));
    ASSERT_TRUE(storage.publish(*busy));
    slot = storage.acquire();
    ASSERT_NE(slot, nullptr);
    ASSERT_EQ(storage.detach_empty(), empty);
    EXPECT_EQ(storage.capacity(), size_t(1));
    EXPECT_EQ(storage.size(), size_t(1));
    EXPECT_EQ(storage.detach_empty(), nullptr);
    ASSERT_TRUE(storage.publish(*empty));
    EXPECT_EQ(storage.capacity(), size_t(2));
    ASSERT_TRUE(storage.release(*slot));
  }
  EXPECT_EQ(state.attempts, size_t(2));
  drain(storage, state);
}

TEST(LlvmLibcAioStorageTest, SlotReuseRequiresExplicitObjectDestruction) {
  State state;
  Allocator allocator{state};
  Storage<1> storage;
  auto *block = Storage<1>::prepare(allocator);
  ASSERT_NE(block, nullptr);
  Storage<1>::Slot *slot;
  {
    Gate gate(state);
    ASSERT_TRUE(storage.publish(*block));
    slot = storage.acquire();
    ASSERT_NE(slot, nullptr);
  }
  Record *old = new (slot->storage()) Record(state, 1);
  EXPECT_EQ(state.constructed, size_t(1));
  EXPECT_EQ(state.destroyed, size_t(0));
  old->~Record();
  {
    Gate gate(state);
    // Destruction alone does not unpin storage before the caller returns it.
    EXPECT_EQ(storage.detach_empty(), nullptr);
    ASSERT_TRUE(storage.release(*slot));
    ASSERT_EQ(storage.acquire(), slot);
  }
  Record *fresh = new (slot->storage()) Record(state, 2);
  EXPECT_EQ(fresh->identity, size_t(2));
  EXPECT_EQ(state.constructed, size_t(2));
  EXPECT_EQ(state.destroyed, size_t(1));
  fresh->~Record();
  {
    Gate gate(state);
    ASSERT_TRUE(storage.release(*slot));
  }
  drain(storage, state);
  EXPECT_EQ(state.destroyed, size_t(2));
}

TEST(LlvmLibcAioStorageTest,
     WrongPoolCannotPublishOrReleaseAnotherPoolsStorage) {
  State state;
  Allocator allocator{state};
  Storage<1> first;
  Storage<1> second;
  auto *block = Storage<1>::prepare(allocator);
  ASSERT_NE(block, nullptr);
  {
    Gate gate(state);
    ASSERT_TRUE(first.publish(*block));
    EXPECT_FALSE(second.publish(*block));
    auto *slot = first.acquire();
    ASSERT_NE(slot, nullptr);
    EXPECT_FALSE(second.release(*slot));
    EXPECT_EQ(first.size(), size_t(1));
    EXPECT_EQ(second.size(), size_t(0));
    ASSERT_TRUE(first.release(*slot));
    ASSERT_EQ(first.detach_empty(), block);
    ASSERT_TRUE(second.publish(*block));
    EXPECT_EQ(first.capacity(), size_t(0));
    EXPECT_EQ(second.capacity(), size_t(1));
  }
  drain(second, state);
}

TEST(LlvmLibcAioStorageTest, DetachedBlocksRetainTheirAllocatorsAfterPoolExit) {
  State first_state;
  State second_state;
  Allocator first_allocator{first_state};
  Allocator second_allocator{second_state};
  Storage<1>::Block *first;
  Storage<1>::Block *second;
  {
    Storage<1> storage;
    first_state.fail = true;
    EXPECT_EQ(Storage<1>::prepare(first_allocator), nullptr);
    {
      Gate gate(first_state);
      EXPECT_EQ(storage.capacity(), size_t(0));
      EXPECT_EQ(storage.acquire(), nullptr);
    }
    first_state.fail = false;
    first = Storage<1>::prepare(first_allocator);
    second = Storage<1>::prepare(second_allocator);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    {
      Gate first_gate(first_state);
      Gate second_gate(second_state);
      ASSERT_TRUE(storage.publish(*first));
      ASSERT_TRUE(storage.publish(*second));
      ASSERT_EQ(storage.detach_empty(), second);
      ASSERT_EQ(storage.detach_empty(), first);
      EXPECT_EQ(storage.capacity(), size_t(0));
    }
  }
  // Neither pool destruction nor detachment invokes an allocator or a T
  // destructor. Detached blocks do not retain a pointer to the old pool.
  EXPECT_EQ(first_state.freed, size_t(0));
  EXPECT_EQ(second_state.freed, size_t(0));
  EXPECT_EQ(first_state.constructed, size_t(0));
  EXPECT_EQ(second_state.constructed, size_t(0));
  ASSERT_TRUE(Storage<1>::dispose(first));
  EXPECT_EQ(first_state.freed, size_t(1));
  EXPECT_EQ(second_state.freed, size_t(0));
  ASSERT_TRUE(Storage<1>::dispose(second));
  EXPECT_EQ(first_state.allocated, first_state.freed);
  EXPECT_EQ(second_state.allocated, second_state.freed);
}
