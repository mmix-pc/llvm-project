#include "src/__support/CPP/limits.h"
#include "src/__support/CPP/type_traits.h"
#include "src/__support/uint128.h"

#ifndef LIBC_TYPES_HAS_INT128
#error Native int128 must be enabled
#endif
static_assert(__SIZEOF_INT128__ == 16);
namespace cpp = LIBC_NAMESPACE::cpp;
static_assert(cpp::is_same_v<UInt128, unsigned __int128>);
static_assert(cpp::is_same_v<Int128, __int128>);
static_assert(cpp::is_integral_v<Int128> && cpp::is_integral_v<UInt128>);
static_assert(cpp::is_same_v<cpp::make_unsigned_t<Int128>, UInt128>);
static_assert(cpp::is_same_v<cpp::make_signed_t<UInt128>, Int128>);
static_assert(cpp::numeric_limits<Int128>::digits == 127);
static_assert(cpp::numeric_limits<UInt128>::digits == 128);
static_assert(cpp::numeric_limits<UInt128>::max() == ~UInt128(0));
static_assert(cpp::numeric_limits<Int128>::min() ==
              -cpp::numeric_limits<Int128>::max() - 1);
constexpr auto Product = LIBC_NAMESPACE::multiword::mul2(~uint64_t(0),
                                                       ~uint64_t(0));
static_assert(Product[0] == 1 && Product[1] == ~uint64_t(1));

extern "C" void libc_product(uint64_t a, uint64_t b, uint64_t *out) {
  auto p = LIBC_NAMESPACE::multiword::mul2(a, b);
  out[0] = p[0];
  out[1] = p[1];
}
