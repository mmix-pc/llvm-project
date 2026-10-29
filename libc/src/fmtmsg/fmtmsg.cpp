//===-- Implementation of fmtmsg ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/fmtmsg/fmtmsg.h"

#include "hdr/fcntl_macros.h"
#include "src/__support/CPP/string_view.h"
#include "src/__support/File/file.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/close.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/open.h"
#include "src/__support/OSUtil/linux/syscall_wrappers/write.h"
#include "src/__support/common.h"
#include "src/stdio/stderr.h"
#include "src/stdlib/getenv.h"
#include <fmtmsg.h>

namespace LIBC_NAMESPACE_DECL {
namespace {

enum Component { LABEL, SEVERITY, TEXT, ACTION, TAG, COMPONENT_COUNT };
constexpr unsigned ALL_COMPONENTS = (1U << COMPONENT_COUNT) - 1;

unsigned selected_components(const char *value) {
  if (!value || !*value)
    return ALL_COMPONENTS;

  constexpr cpp::string_view keywords[] = {"label", "severity", "text",
                                           "action", "tag"};
  unsigned selected = 0;
  while (true) {
    const char *end = value;
    while (*end && *end != ':')
      ++end;
    cpp::string_view word(value, end - value);
    unsigned bit = 0;
    for (unsigned i = 0; i < COMPONENT_COUNT; ++i)
      if (word == keywords[i])
        bit = 1U << i;
    // Empty fields and unknown keywords invalidate the whole list. Repeated
    // valid keywords still select a component only once.
    if (!bit)
      return ALL_COMPONENTS;
    selected |= bit;
    if (!*end)
      return selected;
    value = end + 1;
  }
}

bool valid_label(const char *label) {
  if (!label)
    return true;
  size_t first = 0;
  while (label[first] && label[first] != ':' && first <= 10)
    ++first;
  if (first > 10 || label[first] != ':')
    return false;
  label += first + 1;
  size_t second = 0;
  while (label[second] && label[second] != ':' && second <= 14)
    ++second;
  return second <= 14 && label[second] == '\0';
}

// Use the layout illustrated by POSIX. The punctuation is an implementation
// choice, not an additional requirement on omitted or empty components.
template <typename Write>
bool write_message(Write write, const char *const *components, unsigned mask) {
  bool first_line = false;
  for (unsigned i = LABEL; i <= TEXT; ++i) {
    if (!(mask & (1U << i)) || !components[i])
      continue;
    if (first_line && !write(": "))
      return false;
    if (!write(components[i]))
      return false;
    first_line = true;
  }
  if (first_line && !write("\n"))
    return false;

  bool action = (mask & (1U << ACTION)) && components[ACTION];
  bool tag = (mask & (1U << TAG)) && components[TAG];
  if (action && (!write("TO FIX: ") || !write(components[ACTION])))
    return false;
  if (tag && ((action && !write(" ")) || !write(components[TAG])))
    return false;
  return !(action || tag) || write("\n");
}

bool print_message(const char *const *components, unsigned mask) {
  auto *file = reinterpret_cast<File *>(LIBC_NAMESPACE::stderr);
  File::FileLock lock(file);
  return write_message(
      [file](cpp::string_view part) {
        if (part.empty())
          return true;
        auto result = file->write_unlocked(part.data(), part.size());
        return !result.has_error() && result.value == part.size();
      },
      components, mask);
}

bool console_message(const char *const *components) {
  auto fd =
      linux_syscalls::open("/dev/console", O_WRONLY | O_CLOEXEC | O_NOCTTY, 0);
  if (!fd.has_value())
    return false;
  bool written = write_message(
      [fd = *fd](cpp::string_view part) {
        while (!part.empty()) {
          auto result = linux_syscalls::write(fd, part.data(), part.size());
          if (!result.has_value() || *result == 0)
            return false;
          part.remove_prefix(static_cast<size_t>(*result));
        }
        return true;
      },
      components, ALL_COMPONENTS);
  // Close even after a write failure, and never retry a Linux close.
  auto closed = linux_syscalls::close(*fd);
  return written && closed.has_value();
}

} // namespace

LLVM_LIBC_FUNCTION(int, fmtmsg,
                   (long classification, const char *label, int severity,
                    const char *text, const char *action, const char *tag)) {
  const char *severity_text;
  switch (severity) {
  case MM_NOSEV:
    severity_text = nullptr;
    break;
  case MM_HALT:
    severity_text = "HALT";
    break;
  case MM_ERROR:
    severity_text = "ERROR";
    break;
  case MM_WARNING:
    severity_text = "WARNING";
    break;
  case MM_INFO:
    severity_text = "INFO";
    break;
  default:
    return MM_NOTOK;
  }
  if (!valid_label(label))
    return MM_NOTOK;

  const char *components[] = {label, severity_text, text, action, tag};
  bool print_failed = false;
  bool console_failed = false;
  if (classification & MM_PRINT) {
    // Read the current environment for each call, as with getenv. Environment
    // mutation must be synchronized by the caller; there is no private cache.
    unsigned mask = selected_components(LIBC_NAMESPACE::getenv("MSGVERB"));
    print_failed = !print_message(components, mask);
  }
  if (classification & MM_CONSOLE)
    console_failed = !console_message(components);

  // fmtmsg has return codes, not a specified errno contract. File and raw
  // syscall results remain internal; neither destination suppresses the other.
  if (print_failed && console_failed)
    return MM_NOTOK;
  if (print_failed)
    return MM_NOMSG;
  if (console_failed)
    return MM_NOCON;
  return MM_OK;
}

} // namespace LIBC_NAMESPACE_DECL
