// Adapted from NetBSD lib/libc/db/hash/hash_func.c (hash4) and
// hash.c (__call_hash), revision 5b0e0d5997518246c017c6577adcb59be12d0a79.
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

#include "hash_function.h"

namespace LIBC_NAMESPACE_DECL {
namespace dbm {

uint32_t hash_key(const unsigned char *bytes, size_t size) {
  uint32_t value = 0;
  // Preserve hash4's modulo-2^32 recurrence without its len+7 overflow.
  for (size_t i = 0; i < size; ++i)
    value = value * uint32_t{33} + bytes[i];
  return value;
}

uint32_t hash_key_bucket(const HashHeader &header, const unsigned char *bytes,
                         size_t size) {
  uint32_t bucket = hash_key(bytes, size) & header.high_mask;
  return bucket > header.max_bucket ? bucket & header.low_mask : bucket;
}

uint32_t hash_signature() {
  static constexpr unsigned char key[] = "%$sniglet^&";
  // The persistent signature includes the terminating zero byte.
  return hash_key(key, sizeof(key));
}

} // namespace dbm
} // namespace LIBC_NAMESPACE_DECL
