//===-- MMIX Linux bounded DNS transport ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "hdr/poll_macros.h"
#include "hdr/time_macros.h"
#include "hdr/types/struct_pollfd.h"
#include "hdr/types/struct_timespec.h"
#include "src/netdb/linux/mmix/resolver.h"
namespace LIBC_NAMESPACE_DECL {
namespace mmix::resolver {
struct Socket {
  int fd = -1;
  ~Socket() {
    if (fd >= 0)
      syscall_impl<long>(SYS_close, fd);
  }
};
static int system_error(long result) {
  libc_errno = static_cast<int>(-result);
  return result == -ENOMEM ? EAI_MEMORY : EAI_SYSTEM;
}
static int64_t now() {
  timespec t{};
  long r = syscall_impl<long>(SYS_clock_gettime, CLOCK_MONOTONIC, &t);
  if (r < 0) {
    system_error(r);
    return -1;
  }
  return t.tv_sec * int64_t(1000000000) + t.tv_nsec;
}
static int wait(int fd, short events, int64_t deadline) {
  for (;;) {
    int64_t current = now();
    if (current < 0)
      return EAI_SYSTEM;
    if (current >= deadline)
      return EAI_AGAIN;
    int64_t left = deadline - current;
    timespec timeout{left / 1000000000, left % 1000000000};
    pollfd p{fd, events, 0};
    long r = syscall_impl<long>(SYS_ppoll, &p, 1, &timeout, nullptr, 0);
    if (r == -EINTR)
      continue;
    if (r < 0)
      return system_error(r);
    if (!r)
      return EAI_AGAIN;
    if (p.revents & POLLNVAL)
      return system_error(-EBADF);
    return 0;
  }
}
static int network_error(long result) {
  if (result == -ECONNREFUSED || result == -ECONNRESET ||
      result == -EHOSTUNREACH || result == -ENETUNREACH ||
      result == -ETIMEDOUT || result == -EPIPE)
    return EAI_AGAIN;
  return system_error(result);
}
static int transfer(int fd, unsigned char *data, size_t size, bool writing,
                    int64_t deadline) {
  size_t done = 0;
  while (done < size) {
    int ready = wait(fd, writing ? POLLOUT : POLLIN, deadline);
    if (ready)
      return ready;
    long r = writing ? syscall_impl<long>(SYS_sendto, fd, data + done,
                                          size - done, MSG_NOSIGNAL, nullptr, 0)
                     : syscall_impl<long>(SYS_recvfrom, fd, data + done,
                                          size - done, 0, nullptr, nullptr);
    if (r == -EINTR || r == -EAGAIN)
      continue;
    if (r < 0)
      return network_error(r);
    if (!r)
      return EAI_AGAIN;
    done += static_cast<size_t>(r);
  }
  return 0;
}
static int exchange(const Server &server, unsigned char *query,
                    size_t query_size, unsigned char *reply, size_t &size,
                    bool tcp, int64_t deadline) {
  Socket socket;
  long r = syscall_impl<long>(
      SYS_socket, server.address.ss_family,
      (tcp ? SOCK_STREAM : SOCK_DGRAM) | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (r < 0)
    return system_error(r);
  socket.fd = static_cast<int>(r);
  r = syscall_impl<long>(SYS_connect, socket.fd, &server.address, server.size);
  if (r == -EINPROGRESS || r == -EINTR) {
    int error = wait(socket.fd, POLLOUT, deadline);
    if (error)
      return error;
    int status = 0;
    unsigned length = sizeof(status);
    r = syscall_impl<long>(SYS_getsockopt, socket.fd, SOL_SOCKET, SO_ERROR,
                           &status, &length);
    if (r < 0)
      return system_error(r);
    if (status)
      return network_error(-status);
  } else if (r < 0)
    return network_error(r);
  if (tcp) {
    unsigned char prefix[2] = {static_cast<unsigned char>(query_size >> 8),
                               static_cast<unsigned char>(query_size)};
    int error = transfer(socket.fd, prefix, 2, true, deadline);
    if (error)
      return error;
    error = transfer(socket.fd, query, query_size, true, deadline);
    if (error)
      return error;
    error = transfer(socket.fd, prefix, 2, false, deadline);
    if (error)
      return error;
    size = (size_t(prefix[0]) << 8) | prefix[1];
    if (size < 12)
      return EAI_FAIL;
    return transfer(socket.fd, reply, size, false, deadline);
  }
  for (;;) {
    int error = wait(socket.fd, POLLOUT, deadline);
    if (error)
      return error;
    r = syscall_impl<long>(SYS_sendto, socket.fd, query, query_size, 0, nullptr,
                           0);
    if (r == -EINTR || r == -EAGAIN)
      continue;
    if (r < 0)
      return network_error(r);
    if (static_cast<size_t>(r) != query_size)
      return EAI_FAIL;
    break;
  }
  for (;;) {
    int error = wait(socket.fd, POLLIN, deadline);
    if (error)
      return error;
    r = syscall_impl<long>(SYS_recvfrom, socket.fd, reply, 65535, MSG_TRUNC,
                           nullptr, nullptr);
    if (r == -EINTR || r == -EAGAIN)
      continue;
    if (r < 0)
      return network_error(r);
    if (r < 12 || r > 65535)
      continue;
    size = static_cast<size_t>(r);
    // Connected UDP checks the peer. Also ignore unrelated transactions without
    // renewing the original deadline or issuing another query.
    if (reply[0] != query[0] || reply[1] != query[1] || !(reply[2] & 128) ||
        (reply[2] & 120) || reply[4] != 0 || reply[5] != 1)
      continue;
    char q[256], a[256];
    size_t qo = 12, ao = 12;
    if (!decode_name(query, query_size, qo, q) ||
        !decode_name(reply, size, ao, a) || size - ao < 4 || !same(q, a) ||
        !bytes_equal(query + qo, reply + ao, 4))
      continue;
    return 0;
  }
}
struct Session {
  Config config;
  unsigned char *reply = nullptr;
  int64_t deadline = 0;
  ~Session() { ::free(reply); }
  int init() {
    int error = read_config(config);
    if (error)
      return error;
    reply = static_cast<unsigned char *>(::malloc(65535));
    if (!reply)
      return EAI_MEMORY;
    int64_t current = now();
    if (current < 0)
      return EAI_SYSTEM;
    deadline = current + int64_t(30000000000);
    return 0;
  }
  int query(const char *name, unsigned type, Answers &answer) {
    uint16_t id = 0;
    long r;
    do {
      int64_t current = now();
      if (current < 0)
        return EAI_SYSTEM;
      if (current >= deadline)
        return EAI_AGAIN;
      r = syscall_impl<long>(SYS_getrandom, &id, sizeof(id), 1);
    } while (r == -EINTR);
    if (r < 0)
      return r == -EAGAIN ? EAI_AGAIN : system_error(r);
    if (r != sizeof(id))
      return EAI_AGAIN;
    unsigned char packet[512];
    size_t query_size = make_query(name, type, id, packet);
    if (!query_size)
      return EAI_NONAME;
    for (unsigned round = 0; round < config.attempts; round++)
      for (unsigned i = 0; i < config.server_count; i++) {
        int64_t current = now();
        if (current < 0)
          return EAI_SYSTEM;
        if (current >= deadline)
          return EAI_AGAIN;
        int64_t until = current + int64_t(config.timeout) * 1000000000;
        if (until > deadline)
          until = deadline;
        size_t size = 0;
        int error = exchange(config.servers[i], packet, query_size, reply, size,
                             false, until);
        if (!error && (reply[2] & 2))
          error = exchange(config.servers[i], packet, query_size, reply, size,
                           true, until);
        if (error == EAI_AGAIN)
          continue;
        if (error)
          return error;
        bool alias = false;
        error = parse_answer(reply, size, name, type, id, answer, alias);
        if (error == EAI_AGAIN)
          continue;
        if (error == EAI_NONAME && alias) {
          // A following query keeps the invocation deadline. Detect
          // cross-packet aliases with the caller's visited chain rather than
          // recursive calls.
          return 1;
        }
        return error;
      }
    return EAI_AGAIN;
  }
  int resolve(const char *name, unsigned type, Answers &answer) {
    struct Visited {
      Visited *next;
      char name[256];
    };
    struct History {
      Visited *head = nullptr;
      ~History() {
        while (head) {
          Visited *p = head;
          head = p->next;
          ::free(p);
        }
      }
    } history;
    char current[256];
    if (!copy(current, sizeof(current), name))
      return EAI_NONAME;
    for (;;) {
      for (Visited *p = history.head; p; p = p->next)
        if (same(p->name, current))
          return EAI_FAIL;
      auto *entry = static_cast<Visited *>(::malloc(sizeof(Visited)));
      if (!entry)
        return EAI_MEMORY;
      copy(entry->name, sizeof(entry->name), current);
      entry->next = history.head;
      history.head = entry;
      int result = query(current, type, answer);
      if (result != 1)
        return result;
      copy(current, sizeof(current), answer.canonical);
    }
  }
};
int dns_lookup(const char *name, bool ipv4, bool ipv6, Answers &answers,
               bool v4_fallback) {
  Session session;
  int error = session.init();
  if (error)
    return error;
  size_t n = length(name);
  if (!n || n >= 256)
    return EAI_NONAME;
  bool absolute = name[n - 1] == '.';
  unsigned dots = 0;
  for (size_t i = 0; i < n; i++)
    if (name[i] == '.')
      dots++;
  bool original_first = absolute || dots >= session.config.ndots;
  unsigned suffixes = absolute ? 0 : session.config.search_count;
  for (unsigned candidate = 0; candidate <= suffixes; candidate++) {
    char query[512];
    if ((original_first && candidate == 0) ||
        (!original_first && candidate == suffixes))
      copy(query, sizeof(query), name);
    else {
      unsigned index = original_first ? candidate - 1 : candidate;
      copy(query, sizeof(query), name);
      query[n] = '.';
      copy(query + n + 1, sizeof(query) - n - 1, session.config.search[index]);
    }
    bool success = false;
    if (ipv6) {
      error = session.resolve(query, 28, answers);
      if (error && error != EAI_NONAME)
        return error;
      success = !error;
    }
    char first_canonical[256] = {};
    if (success)
      copy(first_canonical, sizeof(first_canonical), answers.canonical);
    if (ipv4 && !(v4_fallback && success)) {
      error = session.resolve(query, 1, answers);
      if (error && error != EAI_NONAME)
        return error;
      success = success || !error;
    }
    if (success) {
      if (*first_canonical)
        copy(answers.canonical, sizeof(answers.canonical), first_canonical);
      return 0;
    }
    answers.clear();
  }
  return EAI_NONAME;
}
int dns_reverse(const char *name, char output[256]) {
  Session session;
  int error = session.init();
  if (error)
    return error;
  Answers answers;
  error = session.resolve(name, 12, answers);
  if (!error)
    copy(output, 256, answers.canonical);
  return error;
}
} // namespace mmix::resolver
} // namespace LIBC_NAMESPACE_DECL
