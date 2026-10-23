//===-- Precision-bounded printf input tests -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "hdr/sys_mman_macros.h"
#include "hdr/unistd_macros.h"
#include "src/stdio/snprintf.h"
#include "src/sys/mman/mmap.h"
#include "src/sys/mman/mprotect.h"
#include "src/sys/mman/munmap.h"
#include "src/unistd/sysconf.h"
#include "test/UnitTest/Test.h"

TEST(LlvmLibcPrintfPrecisionTest, UnterminatedArrayAtPageBoundary) {
  long page = LIBC_NAMESPACE::sysconf(_SC_PAGESIZE);
  ASSERT_GT(page, 0L);
  size_t size = static_cast<size_t>(page);
  void *mapping =
      LIBC_NAMESPACE::mmap(nullptr, 2 * size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT_NE(mapping, MAP_FAILED);
  char *bytes = static_cast<char *>(mapping);
  int protected_page = LIBC_NAMESPACE::mprotect(bytes + size, size, PROT_NONE);
  if (protected_page != 0) {
    LIBC_NAMESPACE::munmap(mapping, 2 * size);
    ASSERT_EQ(protected_page, 0);
    return;
  }
  bytes[size - 2] = 'A';
  bytes[size - 1] = 'B';
  char output[8];
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.2s",
                                     bytes + size - 2),
            2);
  EXPECT_STREQ(output, "AB");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%4.2s",
                                     bytes + size - 2),
            4);
  EXPECT_STREQ(output, "  AB");
  EXPECT_EQ(LIBC_NAMESPACE::snprintf(output, sizeof(output), "%.0s",
                                     bytes + size - 1),
            0);
  EXPECT_STREQ(output, "");
  EXPECT_EQ(LIBC_NAMESPACE::munmap(mapping, 2 * size), 0);
}
