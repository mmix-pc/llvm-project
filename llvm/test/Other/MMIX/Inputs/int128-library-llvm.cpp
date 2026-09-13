#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/xxhash.h"

static_assert(__SIZEOF_INT128__ == 16);
static_assert(llvm::endianness::native == llvm::endianness::big);
using U = unsigned __int128;
static_assert(std::is_same_v<llvm::common_uint<__int128, unsigned long>, U>);

extern "C" U llvm_bits(__int128 a) { return llvm::bit_cast<U>(a); }

extern "C" U llvm_saturate(U a, U b) {
  return llvm::SaturatingAdd(a, b);
}
extern "C" uint64_t llvm_scaled(uint64_t a, uint64_t b) {
  return llvm::ScaledNumbers::multiply64(a, b).first;
}
extern "C" uint64_t llvm_hash(const uint8_t *data, size_t size) {
  return llvm::xxh3_64bits(llvm::ArrayRef<uint8_t>(data, size));
}
