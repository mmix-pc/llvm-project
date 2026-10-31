//===-- Tests for MMIX AIO transfer adaptation ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/threads/linux/mmix/aio_transfer.h"
#include "test/UnitTest/Test.h"
#include <errno.h>
#include <initializer_list>
#include <sys/socket.h>
#include <unistd.h>

using LIBC_NAMESPACE::Error;
using LIBC_NAMESPACE::ErrorOr;
using LIBC_NAMESPACE::mmix::AioInput;
using LIBC_NAMESPACE::mmix::AioTransfer;
using LIBC_NAMESPACE::mmix::normalize_aio_transfer;

namespace {
struct State {
  unsigned calls = 0;
  unsigned fail_at = 0;
  int failure = EIO;
  int flags = O_RDWR;
  unsigned type = S_IFREG;
  unsigned long fs = TMPFS_MAGIC;
  int pipe_size = 8192;
  int socket_type = SOCK_STREAM;
};
struct Queries {
  State *state;
  int query(int fd, unsigned bit) {
    EXPECT_EQ(fd, 42);
    EXPECT_EQ(state->calls & bit, 0U);
    state->calls |= bit;
    return state->fail_at == bit ? state->failure : 0;
  }
  ErrorOr<int> flags(int fd) {
    int error = query(fd, 1);
    return error ? ErrorOr<int>(Error(error)) : ErrorOr<int>(state->flags);
  }
  ErrorOr<unsigned> type(int fd) {
    int error = query(fd, 2);
    return error ? ErrorOr<unsigned>(Error(error))
                 : ErrorOr<unsigned>(state->type);
  }
  ErrorOr<unsigned long> filesystem(int fd) {
    int error = query(fd, 4);
    return error ? ErrorOr<unsigned long>(Error(error))
                 : ErrorOr<unsigned long>(state->fs);
  }
  ErrorOr<int> pipe(int fd) {
    int error = query(fd, 8);
    return error ? ErrorOr<int>(Error(error)) : ErrorOr<int>(state->pipe_size);
  }
  ErrorOr<int> socket(int fd) {
    int error = query(fd, 16);
    return error ? ErrorOr<int>(Error(error))
                 : ErrorOr<int>(state->socket_type);
  }
};
AioInput input(int opcode = LIO_READ, off_t offset = 17) {
  AioInput result;
  result.opcode = opcode;
  result.offset = offset;
  result.nbytes = 9;
  return result;
}
void failure(State &state, const AioInput &in, int expected, unsigned calls) {
  AioTransfer out{123, 456, 789};
  errno = EDOM;
  EXPECT_EQ(normalize_aio_transfer(42, in, out, Queries{&state}), expected);
  EXPECT_EQ(out.size, uint32_t(123));
  EXPECT_EQ(out.offset, int64_t(456));
  EXPECT_EQ(out.flags, 789U);
  EXPECT_EQ(state.calls, calls);
  EXPECT_EQ(errno, EDOM);
}
} // namespace

TEST(LlvmLibcAioTransferTest, ValidatesPublicInputBeforeQueries) {
  State state;
  failure(state, input(LIO_NOP), EINVAL, 0);
  auto in = input();
  in.nbytes = size_t(SSIZE_MAX) + 1;
  failure(state, in, EINVAL, 0);
}

TEST(LlvmLibcAioTransferTest, AccessModesAndPathDescriptorsAreNotTransfers) {
  const int modes[] = {O_RDONLY, O_WRONLY, O_RDWR, O_ACCMODE, O_PATH | O_RDWR};
  for (int mode : modes) {
    for (int op : {LIO_READ, LIO_WRITE}) {
      State state;
      state.flags = mode;
      const bool permitted =
          mode == O_RDWR || mode == (op == LIO_READ ? O_RDONLY : O_WRONLY);
      if (!permitted)
        failure(state, input(op), EBADF, 1);
      else {
        AioTransfer out{};
        EXPECT_EQ(normalize_aio_transfer(42, input(op), out, Queries{&state}),
                  0);
        EXPECT_EQ(state.calls, 7U);
      }
    }
  }
}

TEST(LlvmLibcAioTransferTest, PositionedFilesRetainOffsetsAndAppendSnapshot) {
  const unsigned long systems[] = {TMPFS_MAGIC, RAMFS_MAGIC, EXT4_SUPER_MAGIC};
  for (auto fs : systems) {
    for (int op : {LIO_READ, LIO_WRITE}) {
      for (bool append : {false, true}) {
        for (off_t offset :
             {off_t(0), off_t(17), off_t(INT64_MAX), off_t(-1), off_t(-2)}) {
          State state;
          state.fs = fs;
          state.flags |= append ? O_APPEND : 0;
          const bool ignored = append && op == LIO_WRITE;
          if (!ignored && offset < 0) {
            failure(state, input(op, offset), EINVAL, 7);
            continue;
          }
          AioTransfer out{};
          EXPECT_EQ(normalize_aio_transfer(42, input(op, offset), out,
                                           Queries{&state}),
                    0);
          EXPECT_EQ(out.size, uint32_t(9));
          EXPECT_EQ(out.offset, ignored ? int64_t(0) : int64_t(offset));
          EXPECT_EQ(out.flags,
                    op == LIO_READ
                        ? 0U
                        : unsigned(append ? RWF_APPEND : RWF_NOAPPEND));
          EXPECT_EQ(state.calls, 7U);
        }
      }
    }
  }
}

TEST(LlvmLibcAioTransferTest,
     StreamsProveOperationsAndKeepReadWriteDistinction) {
  for (unsigned type : {unsigned(S_IFIFO), unsigned(S_IFSOCK)}) {
    for (int op : {LIO_READ, LIO_WRITE}) {
      for (off_t offset : {off_t(-1), off_t(-2), off_t(0), off_t(INT64_MAX)}) {
        State state;
        state.type = type;
        state.flags |= O_APPEND;
        const unsigned calls = 3 | (type == S_IFIFO ? 8 : 16);
        if (op == LIO_READ && offset < 0) {
          failure(state, input(op, offset), EINVAL, calls);
          continue;
        }
        AioTransfer out{};
        EXPECT_EQ(
            normalize_aio_transfer(42, input(op, offset), out, Queries{&state}),
            0);
        EXPECT_EQ(out.offset, int64_t(0));
        EXPECT_EQ(out.flags, 0U);
        EXPECT_EQ(state.calls, calls);
      }
    }
  }
}

TEST(LlvmLibcAioTransferTest, UnknownObjectsDoNotGainAdmissionFromAppend) {
  for (unsigned type : {unsigned(S_IFCHR), unsigned(S_IFBLK), unsigned(S_IFDIR),
                        unsigned(S_IFLNK), 0U}) {
    State state;
    state.type = type;
    state.flags |= O_APPEND;
    failure(state, input(LIO_WRITE, -1), ENOTSUP, 3);
  }
  State state;
  state.fs = 0x12345678;
  state.flags |= O_APPEND;
  failure(state, input(LIO_WRITE, -1), ENOTSUP, 7);
}

TEST(LlvmLibcAioTransferTest,
     QueryErrorsAreNotCapabilitiesOrRetryInstructions) {
  for (unsigned step : {1U, 2U, 4U, 8U, 16U}) {
    for (int error : {EPERM, ENOSYS, EINTR, EIO, ENOTSUP, EBADF, ENOTSOCK}) {
      State state;
      state.type = step == 8 ? S_IFIFO : step == 16 ? S_IFSOCK : S_IFREG;
      state.fail_at = step;
      state.failure = error;
      failure(state, input(), error, step < 4 ? (step == 1 ? 1 : 3) : 3 | step);
    }
  }
}

TEST(LlvmLibcAioTransferTest, MalformedOperationProofIsRejected) {
  State pipe;
  pipe.type = S_IFIFO;
  pipe.pipe_size = 0;
  failure(pipe, input(), EPROTO, 11);
  State socket;
  socket.type = S_IFSOCK;
  socket.socket_type = 0;
  failure(socket, input(), EPROTO, 19);
}

TEST(LlvmLibcAioTransferTest, NativePipeAndSocketQueriesDoNotConsumeData) {
  for (bool socket : {false, true}) {
    int fds[2];
    ASSERT_EQ(socket ? ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) : ::pipe(fds),
              0);
    ASSERT_EQ(::write(fds[1], "x", 1), ssize_t(1));
    AioTransfer out{};
    errno = EDOM;
    EXPECT_EQ(normalize_aio_transfer(fds[0], input(LIO_READ, INT64_MAX), out),
              0);
    EXPECT_EQ(out.offset, int64_t(0));
    EXPECT_EQ(out.flags, 0U);
    EXPECT_EQ(errno, EDOM);
    EXPECT_EQ(normalize_aio_transfer(fds[0], input(LIO_READ, -1), out), EINVAL);
    char byte = 0;
    ASSERT_EQ(::read(fds[0], &byte, 1), ssize_t(1));
    EXPECT_EQ(byte, 'x');
    EXPECT_EQ(normalize_aio_transfer(fds[1], input(LIO_WRITE, -1), out), 0);
    EXPECT_EQ(out.offset, int64_t(0));
    ASSERT_EQ(::close(fds[0]), 0);
    ASSERT_EQ(::close(fds[1]), 0);
  }
}
