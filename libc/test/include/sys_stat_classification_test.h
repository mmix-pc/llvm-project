//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <sys/stat.h>

#ifdef __cplusplus
template <typename T> struct IsInt { static constexpr bool value = false; };
template <> struct IsInt<int> { static constexpr bool value = true; };
#define CHECK_RESULT_TYPE(M) \
  static_assert(IsInt<decltype(M((const struct stat *)0))>::value, "int result")
#else
#define CHECK_RESULT_TYPE(M) \
  _Static_assert(_Generic(M((const struct stat *)0), int: 1, default: 0), \
                 "int result")
#endif

CHECK_RESULT_TYPE(S_TYPEISMQ);
CHECK_RESULT_TYPE(S_TYPEISSEM);
CHECK_RESULT_TYPE(S_TYPEISSHM);
CHECK_RESULT_TYPE(S_TYPEISTMO);

#undef CHECK_RESULT_TYPE

// Compile with -DINVALID_CLASSIFIER=<macro> for the negative argument probe.
#ifdef INVALID_CLASSIFIER
static int invalid_argument(void) { return INVALID_CLASSIFIER(1); }
#endif

static int check_classification(void) {
  const mode_t modes[] = {0, S_IFREG, S_IFDIR, S_IFCHR, S_IFBLK,
                          S_IFIFO, S_IFLNK, S_IFSOCK};
  struct stat st;
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
    st.st_mode = modes[i] | 0644;
    const struct stat *ptr = &st;
    if (S_TYPEISMQ(ptr) || S_TYPEISSEM(ptr) || S_TYPEISSHM(ptr) ||
        S_TYPEISTMO(ptr))
      return 1;
    if (st.st_mode != (modes[i] | 0644))
      return 2;
  }
  struct stat entries[5];
  for (unsigned i = 0; i < 5; ++i)
    entries[i].st_mode = 0;
  struct stat *next = entries;
  if (S_TYPEISMQ(next++) || next != entries + 1)
    return 3;
  if (S_TYPEISSEM(next++) || next != entries + 2)
    return 4;
  if (S_TYPEISSHM(next++) || next != entries + 3)
    return 5;
  if (S_TYPEISTMO(next++) || next != entries + 4)
    return 6;
  return 0;
}
