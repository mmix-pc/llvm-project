// Adapted from NetBSD lib/libc/db/hash/hash.c at
// 5b0e0d5997518246c017c6577adcb59be12d0a79 (__expand_table).
/*-
 * Copyright (c) 1990, 1993, 1994
 * The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Margo Seltzer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "hash_growth.h"
#include "hash_function.h"
#include "hdr/errno_macros.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

ErrorOr<HashSplit> plan_hash_split(const HashHeader &header) {
  unsigned char encoded[HASH_HEADER_SIZE];
  if (!encode_hash_header(header, hash_signature(), encoded, sizeof(encoded)))
    return Error(EINVAL);
  HashSplit split{header, 0, header.max_bucket + 1, 0};
  split.old_bucket = split.new_bucket & header.low_mask;
  auto &next = split.header;
  next.max_bucket = split.new_bucket;

  // Ceil(log2(new_bucket + 1)), without signed shifts or integer addition
  // outside the range already established by header validation.
  uint32_t spare_index = 0;
  for (uint32_t value = split.new_bucket; value; value >>= 1)
    ++spare_index;
  if (spare_index >= 31)
    return Error(EFBIG);
  if (spare_index > header.overflow_point) {
    next.spares[spare_index] = header.spares[header.overflow_point];
    next.overflow_point = spare_index;
  }
  if (split.new_bucket / 256 >= header.directory_size)
    next.directory_size *= 2;
  if (split.new_bucket > header.high_mask) {
    next.low_mask = header.high_mask;
    next.high_mask = split.new_bucket | header.high_mask;
  }
  if (!encode_hash_header(next, hash_signature(), encoded, sizeof(encoded)) ||
      !hash_bucket_page(next, split.new_bucket, split.new_page))
    return Error(EFBIG);
  return split;
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
