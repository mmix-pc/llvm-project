//===-- MMIX Linux network database provider ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "src/netdb/getservbyport.h"
#include "src/__support/common.h"
#include "src/netdb/linux/mmix/databases.h"
namespace LIBC_NAMESPACE_DECL {
LLVM_LIBC_FUNCTION(servent *, getservbyport,
                   (int number, const char *protocol)) {
  using namespace mmix::netdb;
  services.rewind(services.stay);
  servent *item;
  while ((item = next_service())) {
    if (item->s_port == number &&
        (!protocol || mmix::netdb::equal(item->s_proto, protocol)))
      break;
  }
  if (!services.stay)
    services.close_file();
  return item;
}
} // namespace LIBC_NAMESPACE_DECL
