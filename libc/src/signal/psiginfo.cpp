//===-- Implementation of psiginfo ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/signal/psiginfo.h"
#include "hdr/signal_macros.h"
#include "hdr/stdint_proxy.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/CPP/stringstream.h"
#include "src/__support/File/file.h"
#include "src/__support/StringUtil/diagnostic_message.h"
#include "src/__support/StringUtil/platform_signals.h"
#include "src/__support/StringUtil/signal_to_string.h"
#include "src/__support/common.h"
#include "src/__support/integer_to_string.h"
#include "src/__support/libc_errno.h"
#include "src/stdio/stderr.h"

namespace LIBC_NAMESPACE_DECL {
namespace {

enum class Payload { NONE, SENDER, QUEUED, TIMER, VALUE, FAULT, CHILD, BAND };

struct Cause {
  int signo; // Zero matches this origin for any known signal.
  int code;
  const char *description;
  Payload payload;
};

constexpr Cause CAUSES[] = {
    {0, SI_USER, "sent by kill()", Payload::SENDER},
#ifdef SI_TKILL
    {0, SI_TKILL, "sent by tkill()/tgkill()", Payload::SENDER},
#endif
    {0, SI_QUEUE, "sent by sigqueue()", Payload::QUEUED},
    {0, SI_TIMER, "timer expired", Payload::TIMER},
    {0, SI_MESGQ, "message queue notification", Payload::QUEUED},
    {0, SI_ASYNCIO, "asynchronous I/O completed", Payload::VALUE},
#ifdef SI_KERNEL
    {0, SI_KERNEL, "sent by kernel", Payload::NONE},
#endif
#ifdef SI_ASYNCNL
    {0, SI_ASYNCNL, "asynchronous name lookup completed", Payload::NONE},
#endif
#ifdef SI_SIGIO
    {0, SI_SIGIO, "queued I/O notification", Payload::BAND},
#endif
    {SIGILL, ILL_ILLOPC, "illegal opcode", Payload::FAULT},
    {SIGILL, ILL_ILLOPN, "illegal operand", Payload::FAULT},
    {SIGILL, ILL_ILLADR, "illegal addressing mode", Payload::FAULT},
    {SIGILL, ILL_ILLTRP, "illegal trap", Payload::FAULT},
    {SIGILL, ILL_PRVOPC, "privileged opcode", Payload::FAULT},
    {SIGILL, ILL_PRVREG, "privileged register", Payload::FAULT},
    {SIGILL, ILL_COPROC, "coprocessor error", Payload::FAULT},
    {SIGILL, ILL_BADSTK, "invalid stack", Payload::FAULT},
    {SIGFPE, FPE_INTDIV, "integer division by zero", Payload::FAULT},
    {SIGFPE, FPE_INTOVF, "integer overflow", Payload::FAULT},
    {SIGFPE, FPE_FLTDIV, "floating-point division by zero", Payload::FAULT},
    {SIGFPE, FPE_FLTOVF, "floating-point overflow", Payload::FAULT},
    {SIGFPE, FPE_FLTUND, "floating-point underflow", Payload::FAULT},
    {SIGFPE, FPE_FLTRES, "inexact floating-point result", Payload::FAULT},
    {SIGFPE, FPE_FLTINV, "invalid floating-point operation", Payload::FAULT},
    {SIGFPE, FPE_FLTSUB, "subscript out of range", Payload::FAULT},
    {SIGSEGV, SEGV_MAPERR, "unmapped address", Payload::FAULT},
    {SIGSEGV, SEGV_ACCERR, "invalid memory permissions", Payload::FAULT},
    {SIGBUS, BUS_ADRALN, "misaligned address", Payload::FAULT},
    {SIGBUS, BUS_ADRERR, "nonexistent physical address", Payload::FAULT},
    {SIGBUS, BUS_OBJERR, "object-specific hardware error", Payload::FAULT},
    {SIGTRAP, TRAP_BRKPT, "breakpoint", Payload::FAULT},
    {SIGTRAP, TRAP_TRACE, "trace trap", Payload::FAULT},
    {SIGCHLD, CLD_EXITED, "child exited", Payload::CHILD},
    {SIGCHLD, CLD_KILLED, "child killed", Payload::CHILD},
    {SIGCHLD, CLD_DUMPED, "child dumped core", Payload::CHILD},
    {SIGCHLD, CLD_TRAPPED, "child trapped", Payload::CHILD},
    {SIGCHLD, CLD_STOPPED, "child stopped", Payload::CHILD},
    {SIGCHLD, CLD_CONTINUED, "child continued", Payload::CHILD},
    {SIGPOLL, POLL_IN, "input available", Payload::BAND},
    {SIGPOLL, POLL_OUT, "output available", Payload::BAND},
    {SIGPOLL, POLL_MSG, "input message available", Payload::BAND},
    {SIGPOLL, POLL_ERR, "I/O error", Payload::NONE},
    {SIGPOLL, POLL_PRI, "priority input available", Payload::NONE},
    {SIGPOLL, POLL_HUP, "device disconnected", Payload::NONE},
};

bool known_signal(int signo) {
  if (signo >= SIGRTMIN && signo <= SIGRTMAX)
    return true;
  for (const auto &signal : PLATFORM_SIGNALS)
    if (signo > 0 && signal.num == signo)
      return true;
  return false;
}

const Cause *find_cause(int signo, int code) {
  if (known_signal(signo)) {
    for (const auto &cause : CAUSES)
      if (cause.code == code && (cause.signo == 0 || cause.signo == signo))
        return &cause;
  }
  return nullptr;
}

void format_payload(cpp::StringStream &out, const siginfo_t &info,
                    Payload payload) {
  // The origin/code, not just the signal number, selects the union layout.
  // sigval has no discriminator, so label its integer interpretation explicitly.
  switch (payload) {
  case Payload::QUEUED:
  case Payload::SENDER:
    out << ", pid=" << info.si_pid << ", uid=" << info.si_uid;
    if (payload == Payload::QUEUED)
      out << ", value(int)=" << info.si_value.sival_int;
    break;
  case Payload::TIMER:
    out << ", timerid=" << info.si_timerid << ", overrun=" << info.si_overrun
        << ", value(int)=" << info.si_value.sival_int;
    break;
  case Payload::VALUE:
    out << ", value(int)=" << info.si_value.sival_int;
    break;
  case Payload::FAULT: {
    const IntegerToString<uintptr_t, radix::Hex::WithPrefix> address(
        reinterpret_cast<uintptr_t>(info.si_addr));
    out << ", addr=" << address.view();
    break;
  }
  case Payload::CHILD:
    out << ", pid=" << info.si_pid << ", uid=" << info.si_uid
        << ", status=" << info.si_status;
    break;
  case Payload::BAND:
    out << ", band=" << info.si_band;
    break;
  case Payload::NONE:
    break;
  }
}

} // namespace

LLVM_LIBC_FUNCTION(void, psiginfo,
                   (const siginfo_t *info, const char *message)) {
  const int saved_errno = libc_errno;
  // Keep strsignal's borrowed storage intact if it is used as the prefix.
  char signal_buffer[sizeof("Real-time signal") + 1 +
                     IntegerToString<int>::buffer_size()];
  const char *description = get_diagnostic_message(
      get_signal_string(info->si_signo, signal_buffer).data());
  const Cause *cause = find_cause(info->si_signo, info->si_code);
  // Each payload has at most three integer fields (or one pointer). Labels
  // fit in 128 bytes; prefix and translated descriptions are separate fragments.
  char payload_buffer[128 + 3 * IntegerToString<uintmax_t>::buffer_size()];
  cpp::StringStream payload(payload_buffer);
  if (cause)
    format_payload(payload, *info, cause->payload);
  else
    payload << info->si_code;

  const bool has_prefix = message && *message;
  const cpp::string_view parts[] = {
      has_prefix ? message : "", has_prefix ? ": " : "", description, " (",
      cause ? get_diagnostic_message(cause->description) : "code=",
      payload.str(), ")\n"};
  File *file = reinterpret_cast<File *>(stderr);
  File::FileLock lock(file);
  for (cpp::string_view part : parts) {
    if (part.empty())
      continue;
    auto result = file->write_diagnostic_unlocked(part.data(), part.size());
    if (result.has_error()) {
      libc_errno = result.error;
      return;
    }
  }
  libc_errno = saved_errno;
}

} // namespace LIBC_NAMESPACE_DECL
