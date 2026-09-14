#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/MemAlloc.h"
#include <cstdlib>
#include <new>

extern "C" void *llvm_allocation(size_t size) {
  void *p = llvm::safe_malloc(size);
  p = llvm::safe_realloc(p, size + 1);
  std::free(p);
  return llvm::safe_calloc(size, 2);
}

extern "C" unsigned vector_allocation(unsigned count) {
  llvm::SmallVector<unsigned, 4> values;
  for (unsigned i = 0; i != count; ++i)
    values.push_back(i);
  return values.empty() ? 0 : values.back();
}

void *ordinary_new(size_t size) { return ::operator new(size); }
void *nothrow_new(size_t size) { return ::operator new(size, std::nothrow); }
void ordinary_delete(void *p) { ::operator delete(p); }

// Explicit runtime functions, not the excluded implicit aligned expressions.
void *aligned_new(size_t size) {
  return ::operator new(size, std::align_val_t(64));
}
void aligned_delete(void *p) { ::operator delete(p, std::align_val_t(64)); }
