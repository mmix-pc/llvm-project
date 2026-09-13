#include <__random/linear_congruential_engine.h>
#include <limits>
#include <type_traits>

#if !defined(__SIZEOF_INT128__) || !_LIBCPP_HAS_INT128
#error Native int128 must be enabled
#endif
using S = __int128;
using U = unsigned __int128;
static_assert(std::is_integral_v<S> && std::is_integral_v<U>);
static_assert(std::is_signed_v<S> && std::is_unsigned_v<U>);
static_assert(std::is_same_v<std::make_unsigned_t<S>, U>);
static_assert(std::is_same_v<std::make_signed_t<U>, S>);
static_assert(std::numeric_limits<S>::digits == 127);
static_assert(std::numeric_limits<U>::digits == 128);
static_assert(std::numeric_limits<U>::max() == ~U(0));
static_assert(std::numeric_limits<S>::min() ==
              -std::numeric_limits<S>::max() - 1);

// Neither a power-of-two modulus nor the Schrage algorithm: the upstream
// engine must use its newly available unsigned-int128 multiply/remainder.
constexpr unsigned long long A = 0xffffffffffffffc0ULL;
constexpr unsigned long long M = 0xffffffffffffffc5ULL;
static_assert(std::__lce_alg_picker<A, 1, M, ~0ULL>::__mode ==
              std::_LCE_Promote);
extern "C" unsigned long long libcxx_next(unsigned long long seed) {
  std::linear_congruential_engine<unsigned long long, A, 1, M> engine(seed);
  return engine();
}
