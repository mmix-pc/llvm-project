//===-- Freestanding loader handoff checks
//---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

extern unsigned long loader_imported_data;
extern unsigned long loader_imported_call(unsigned long);

static int equal(const char *a, const char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

__attribute__((visibility("hidden"))) unsigned long
loader_check_handoff(const unsigned long *sp) {
  if (*sp != 2)
    return 2;
  const char *const *argv = (const char *const *)(sp + 1);
  if (!argv[0] || !argv[1] || argv[2] || !equal(argv[1], "loader-handoff"))
    return 3;
  const char *const *envp = argv + 3;
  int found = 0;
  while (*envp) {
    found |= equal(*envp, "LOADER_HANDOFF=1");
    ++envp;
  }
  if (!found || loader_imported_data != 42 || loader_imported_call(17) != 59)
    return 4;
  // Decode auxv directly, without a public libc provider or startup wrapper.
  const unsigned long *aux = (const unsigned long *)(envp + 1);
  unsigned long entry = 0;
  while (aux[0]) {
    if (aux[0] == 9) // AT_ENTRY
      entry = aux[1];
    aux += 2;
  }
  extern void _start(void);
  return entry != (unsigned long)&_start;
}
