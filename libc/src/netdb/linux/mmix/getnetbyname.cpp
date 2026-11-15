//===-- MMIX Linux network database provider ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/getnetbyname.h"
#include "src/__support/common.h"
#include "src/netdb/linux/mmix/databases.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(netent *, getnetbyname, (const char *name)) {
  using namespace mmix::netdb;
  networks.rewind(networks.stay);
  netent *item;
  while ((item = next_net())) {
    if (mmix::netdb::equal(item->n_name, name))
      break;
  }
  if (!networks.stay)
    networks.close_file();
  return item;
}
} // namespace LIBC_NAMESPACE_DECL
