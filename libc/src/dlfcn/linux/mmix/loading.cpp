//===-- MMIX runtime loading API state ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "loading.h"
#include "hdr/errno_macros.h"
#include "src/__support/threads/linux/mmix/loader_cancellation.h"

namespace LIBC_NAMESPACE_DECL {
namespace mmix {
static loader::LoaderAPI api{};
struct ErrorState {
  char text[96];
  bool pending;
};
// Startup-resident IE storage must not recurse through the late TLS resolver.
static thread_local
    __attribute__((tls_model("initial-exec"))) ErrorState error_state{};

bool adopt_loading_services(const loader::LoaderAPI &input) {
  if (api.context || !input.context || !input.open || !input.lookup)
    return false;
  api = input;
  return true;
}

static void set_error(long error) {
  const char *message;
  switch (error) {
  case EINVAL:
    message = "invalid loader argument, mode, caller or handle";
    break;
  case ENOENT:
    message = "loader object or symbol not found";
    break;
  case ENOMEM:
    message = "loader allocation failed";
    break;
  case ENOEXEC:
    message = "invalid or unsupported loader image";
    break;
  case ENOSYS:
    message = "runtime loading service is unavailable";
    break;
  case ENAMETOOLONG:
    message = "loader pathname is too long";
    break;
  default:
    message = "loader operation failed";
    break;
  }
  unsigned i = 0;
  do {
    error_state.text[i] = message[i];
  } while (message[i++] && i < sizeof(error_state.text) - 1);
  error_state.text[sizeof(error_state.text) - 1] = 0;
  error_state.pending = true;
}

class CancellationScope {
  loader::LoaderCancellation hooks = loader_cancellation_hooks();
  uint64_t saved = 0;
  long status;

public:
  CancellationScope()
      : status(hooks.enter && hooks.leave ? hooks.enter(&saved) : -EINVAL) {}
  ~CancellationScope() {
    // Runtime locks are already gone and diagnostics copied into this thread.
    if (!status && hooks.leave(saved))
      __builtin_trap();
  }
  long error() const { return status < 0 ? -status : status; }
  loader::LoaderCancellation callbacks() const { return hooks; }
};

void *loader_open(const char *name, int flags, uintptr_t caller) {
  CancellationScope guard;
  if (guard.error()) {
    set_error(guard.error());
    return nullptr;
  }
  error_state.pending = false;
  uint64_t token = 0;
  long error = !loader::valid_open_flags(static_cast<unsigned>(flags)) ? EINVAL
               : !api.open ? ENOSYS
                           : api.open(api.context, name, flags, caller,
                                      guard.callbacks(), &token);
  if (!error && (!token || token == loader::LookupNext))
    error = EINVAL;
  if (error) {
    set_error(error);
    return nullptr;
  }
  // A constructor may have handled a nested loading failure. The completed
  // outer operation owns the status returned to its caller.
  error_state.pending = false;
  return reinterpret_cast<void *>(token);
}

void *loader_lookup(void *handle, const char *name, uintptr_t caller) {
  CancellationScope guard;
  if (guard.error()) {
    set_error(guard.error());
    return nullptr;
  }
  error_state.pending = false;
  uintptr_t result = 0;
  long error =
      !name ? EINVAL
      : !api.lookup
          ? ENOSYS
          : api.lookup(api.context, reinterpret_cast<uintptr_t>(handle), name,
                       caller, guard.callbacks(), &result);
  if (error) {
    set_error(error);
    return nullptr;
  }
  error_state.pending = false;
  return reinterpret_cast<void *>(result);
}

char *loader_error() {
  CancellationScope guard;
  if (guard.error()) {
    set_error(guard.error());
    return nullptr;
  }
  if (!error_state.pending)
    return nullptr;
  error_state.pending = false;
  return error_state.text;
}
} // namespace mmix
} // namespace LIBC_NAMESPACE_DECL
