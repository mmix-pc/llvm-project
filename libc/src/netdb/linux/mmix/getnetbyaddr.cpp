//===-- MMIX Linux network database provider ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/getnetbyaddr.h"
#include "src/__support/common.h"
#include "src/netdb/linux/mmix/databases.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(netent *, getnetbyaddr, (uint32_t number, int type)) {
  using namespace mmix::netdb;
  networks.rewind(networks.stay);
  netent *item;
  while ((item = next_net())) {
    if (item->n_net == number && item->n_addrtype == type)
      break;
  }
  if (!networks.stay)
    networks.close_file();
  return item;
}
} // namespace LIBC_NAMESPACE_DECL
