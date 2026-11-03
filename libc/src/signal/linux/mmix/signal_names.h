//===-- MMIX Linux public signal names ---------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_NAMES_H
#define LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_NAMES_H

#include "hdr/signal_macros.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {

struct SignalName {
  int number;
  const char *name;
};

// Keep standard spellings before aliases so output has one canonical name.
inline constexpr SignalName SIGNAL_NAMES[] = {
    {SIGHUP, "HUP"},       {SIGINT, "INT"},       {SIGQUIT, "QUIT"},
    {SIGILL, "ILL"},       {SIGTRAP, "TRAP"},     {SIGABRT, "ABRT"},
    {SIGBUS, "BUS"},       {SIGFPE, "FPE"},       {SIGKILL, "KILL"},
    {SIGUSR1, "USR1"},     {SIGSEGV, "SEGV"},     {SIGUSR2, "USR2"},
    {SIGPIPE, "PIPE"},     {SIGALRM, "ALRM"},     {SIGTERM, "TERM"},
    {SIGSTKFLT, "STKFLT"}, {SIGCHLD, "CHLD"},     {SIGCONT, "CONT"},
    {SIGSTOP, "STOP"},     {SIGTSTP, "TSTP"},     {SIGTTIN, "TTIN"},
    {SIGTTOU, "TTOU"},     {SIGURG, "URG"},       {SIGXCPU, "XCPU"},
    {SIGXFSZ, "XFSZ"},     {SIGVTALRM, "VTALRM"}, {SIGPROF, "PROF"},
    {SIGWINCH, "WINCH"},   {SIGPOLL, "POLL"},     {SIGPWR, "PWR"},
    {SIGSYS, "SYS"},       {SIGIOT, "IOT"},       {SIGIO, "IO"}};

inline bool signal_name_equal(const char *left, const char *right) {
  while (*left && *left == *right) {
    ++left;
    ++right;
  }
  return *left == *right;
}

inline bool public_signal_number(int number) {
  if (number >= SIGRTMIN && number <= SIGRTMAX)
    return true;
  for (const auto &entry : SIGNAL_NAMES)
    if (entry.number == number)
      return true;
  return false;
}

inline bool parse_signal_decimal(const char *text, unsigned limit,
                                 unsigned &value) {
  if (!*text)
    return false;
  unsigned result = 0;
  for (; *text; ++text) {
    if (*text < '0' || *text > '9')
      return false;
    unsigned digit = static_cast<unsigned>(*text - '0');
    if (result > limit / 10 || (result == limit / 10 && digit > limit % 10))
      return false;
    result = result * 10 + digit;
  }
  value = result;
  return true;
}

inline int signal_from_name(const char *text) {
  for (const auto &entry : SIGNAL_NAMES)
    if (signal_name_equal(text, entry.name))
      return entry.number;
  const char *prefixes[] = {"RTMIN", "RTMAX"};
  for (unsigned end = 0; end < 2; ++end) {
    const char *suffix = text;
    const char *prefix = prefixes[end];
    while (*prefix && *suffix == *prefix) {
      ++prefix;
      ++suffix;
    }
    if (*prefix)
      continue;
    int base = end ? SIGRTMAX : SIGRTMIN;
    if (!*suffix)
      return base;
    if (*suffix++ != (end ? '-' : '+'))
      return -1;
    unsigned offset;
    if (!parse_signal_decimal(suffix, SIGRTMAX - SIGRTMIN, offset))
      return -1;
    return end ? base - static_cast<int>(offset)
               : base + static_cast<int>(offset);
  }
  unsigned number;
  if (!parse_signal_decimal(text, SIGRTMAX, number) ||
      !public_signal_number(static_cast<int>(number)))
    return -1;
  return static_cast<int>(number);
}

inline bool signal_to_name(int number, char *output) {
  const char *name = nullptr;
  for (const auto &entry : SIGNAL_NAMES)
    if (entry.number == number) {
      name = entry.name;
      break;
    }
  unsigned offset = 0;
  char separator = '+';
  if (!name) {
    if (number < SIGRTMIN || number > SIGRTMAX)
      return false;
    unsigned low = static_cast<unsigned>(number - SIGRTMIN);
    unsigned high = static_cast<unsigned>(SIGRTMAX - number);
    bool upper = high < low;
    name = upper ? "RTMAX" : "RTMIN";
    offset = upper ? high : low;
    separator = upper ? '-' : '+';
  }
  while (*name)
    *output++ = *name++;
  if (offset) {
    *output++ = separator;
    char digits[10];
    unsigned size = 0;
    do {
      digits[size++] = static_cast<char>('0' + offset % 10);
      offset /= 10;
    } while (offset);
    while (size)
      *output++ = digits[--size];
  }
  *output = '\0';
  return true;
}

} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC_SIGNAL_LINUX_MMIX_SIGNAL_NAMES_H
