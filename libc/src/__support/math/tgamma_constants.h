//===-- High-precision gamma Taylor coefficients ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMA_CONSTANTS_H
#define LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMA_CONSTANTS_H

#include "src/__support/FPUtil/dyadic_float.h"
#include "src/__support/integer_literals.h"
#include "src/__support/macros/config.h"

namespace LIBC_NAMESPACE_DECL {
namespace math {
namespace gamma_double_internal {

using GammaWide = fputil::DyadicFloat<128>;
using LIBC_NAMESPACE::operator""_u128;

// Gamma(1+z) = sum(c[n]*z^n), |z| <= 1/2.
// Generated with MPFR at 768-bit precision, rounded once to 128 bits (RN).
// No implementation or coefficient table from another libm is used.
//
// Reproduction:
//   a[1] = -EulerGamma
//   a[k] = (-1)^k * zeta(k), k >= 2
//   c[0] = 1
//   c[n] = sum(a[k] * c[n-k], k=1..n) / n, n=1..192
//   Store each c[n] as sign * integer_mantissa * 2^exponent.
//
// Cauchy's bound on |z|=7/8 gives |c[n]| <= 8*(8/7)^n: the
// Euler integral bounds |Gamma(1+z)| by Gamma(Re(1+z)) <= 8 there.
// Thus the degree-192 Taylor truncation on |z|<=1/2 is bounded by
// 8*(4/7)^193/(1-4/7) < 2^-151. This bounds truncation only, not the
// rounding of coefficients, evaluation, recurrence, or final conversion.
LIBC_INLINE_VAR constexpr GammaWide TGAMMA_COEFFS[] = {
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 0
    {Sign::NEG, -128, 0x93c467e37db0c7a4'd1be3f810152cb57_u128}, // 1
    {Sign::POS, -128, 0xfd32c611d8abc3bb'2568cb5044147f59_u128}, // 2
    {Sign::NEG, -128, 0xe8508c79925b17b1'6c2df227dac12bc5_u128}, // 3
    {Sign::POS, -128, 0xfb52882a84b5a9fa'be4f736915713840_u128}, // 4
    {Sign::NEG, -128, 0xfb640761c5b3d46c'070d4073d7b86ed5_u128}, // 5
    {Sign::POS, -128, 0xfe3f0537598857be'f7318a74637b9dbb_u128}, // 6
    {Sign::NEG, -128, 0xfef9f8abdbd1cadf'a1b17b8183c8d56d_u128}, // 7
    {Sign::POS, -128, 0xff83dad0bffb5cc8'dda41ed8d45e56e4_u128}, // 8
    {Sign::NEG, -128, 0xffc01eb4505e9fc3'3b20a10fe00e8ba0_u128}, // 9
    {Sign::POS, -128, 0xffe0420eac2d1522'a2adf76f4eb7f4be_u128}, // 10
    {Sign::NEG, -128, 0xfff00c62427a3b4b'5540d6c46a0295b7_u128}, // 11
    {Sign::POS, -128, 0xfff805bb478e1eb8'6bf92b7a665477e2_u128}, // 12
    {Sign::NEG, -128, 0xfffc01aac2701198'e7cef3b0583aae3f_u128}, // 13
    {Sign::POS, -128, 0xfffe0097ca820962'47f92f47f028b39d_u128}, // 14
    {Sign::NEG, -128, 0xffff003157ac4121'f55db4c561b5afb6_u128}, // 15
    {Sign::POS, -128, 0xffff8010a312ba10'69b15dbdf9715413_u128}, // 16
    {Sign::NEG, -128, 0xffffc00586c8cff5'0ce62de3935bc942_u128}, // 17
    {Sign::POS, -128, 0xffffe001d887ba8e'09a02cb2c11cd084_u128}, // 18
    {Sign::NEG, -128, 0xfffff0009d7af9a8'cf20cdf8717bde17_u128}, // 19
    {Sign::POS, -128, 0xfffff80034845aca'f150f8b174c178c5_u128}, // 20
    {Sign::NEG, -128, 0xfffffc0011820fcf'ad755c561dd5537e_u128}, // 21
    {Sign::POS, -128, 0xfffffe0005d64588'6721ae1afa601ad7_u128}, // 22
    {Sign::NEG, -128, 0xffffff0001f22495'dce7192daf993ea2_u128}, // 23
    {Sign::POS, -128, 0xffffff8000a60fd2'8a34a57773feeaa9_u128}, // 24
    {Sign::NEG, -128, 0xffffffc000375b7c'c4c263f2644a9363_u128}, // 25
    {Sign::POS, -128, 0xffffffe00012740d'430b99709fea5d4e_u128}, // 26
    {Sign::NEG, -128, 0xfffffff0000626bd'4758e4c48f5c218c_u128}, // 27
    {Sign::POS, -128, 0xfffffff800020ced'505c5b8987981f49_u128}, // 28
    {Sign::NEG, -128, 0xfffffffc0000aefa'a8d5899096e59693_u128}, // 29
    {Sign::POS, -128, 0xfffffffe00003a53'c6799d374bbd8e58_u128}, // 30
    {Sign::NEG, -128, 0xffffffff00001371'505fec65b76d3409_u128}, // 31
    {Sign::POS, -128, 0xffffffff8000067b'1e589487b304ff35_u128}, // 32
    {Sign::NEG, -128, 0xffffffffc0000229'0b010633b1c9cece_u128}, // 33
    {Sign::POS, -128, 0xffffffffe00000b8'593938030c2269c6_u128}, // 34
    {Sign::NEG, -128, 0xfffffffff000003d'73214af7e8980c21_u128}, // 35
    {Sign::POS, -128, 0xfffffffff8000014'7bb951c2a2292f34_u128}, // 36
    {Sign::NEG, -128, 0xfffffffffc000006'd3e9541e628415c3_u128}, // 37
    {Sign::POS, -128, 0xfffffffffe000002'46a354ec796b76a9_u128}, // 38
    {Sign::NEG, -128, 0xffffffffff000000'c2367fdcce1805e1_u128}, // 39
    {Sign::POS, -128, 0xffffffffff800000'40bcd8d7c68875d8_u128}, // 40
    {Sign::NEG, -128, 0xffffffffffc00000'1594492b7896cc71_u128}, // 41
    {Sign::POS, -128, 0xffffffffffe00000'07316df20b41a314_u128}, // 42
    {Sign::NEG, -128, 0xfffffffffff00000'0265cf5ee73565a8_u128}, // 43
    {Sign::POS, -128, 0xfffffffffff80000'00cc9a7885f047c5_u128}, // 44
    {Sign::NEG, -128, 0xfffffffffffc0000'0044337e6587f7ac_u128}, // 45
    {Sign::POS, -128, 0xfffffffffffe0000'0016bbd505660227_u128}, // 46
    {Sign::NEG, -128, 0xffffffffffff0000'000793f1bab02f37_u128}, // 47
    {Sign::POS, -128, 0xffffffffffff8000'000286a5ec739be2_u128}, // 48
    {Sign::NEG, -128, 0xffffffffffffc000'0000d78ca50a171a_u128}, // 49
    {Sign::POS, -128, 0xffffffffffffe000'000047d98c9195d7_u128}, // 50
    {Sign::NEG, -128, 0xfffffffffffff000'000017f32ee96ad2_u128}, // 51
    {Sign::POS, -128, 0xfffffffffffff800'000007fbba515c7e_u128}, // 52
    {Sign::NEG, -128, 0xfffffffffffffc00'000002a93e1c0263_u128}, // 53
    {Sign::POS, -128, 0xfffffffffffffe00'000000e314b439af_u128}, // 54
    {Sign::NEG, -128, 0xffffffffffffff00'0000004bb19176c9_u128}, // 55
    {Sign::POS, -128, 0xffffffffffffff80'000000193b30807c_u128}, // 56
    {Sign::NEG, -128, 0xffffffffffffffc0'0000000869102bb7_u128}, // 57
    {Sign::POS, -128, 0xffffffffffffffe0'00000002cdb00ecb_u128}, // 58
    {Sign::NEG, -128, 0xfffffffffffffff0'00000000ef3aafa7_u128}, // 59
    {Sign::POS, -128, 0xfffffffffffffff8'000000004fbe3a91_u128}, // 60
    {Sign::NEG, -128, 0xfffffffffffffffc'000000001a94be31_u128}, // 61
    {Sign::POS, -128, 0xfffffffffffffffe'0000000008dc3f66_u128}, // 62
    {Sign::NEG, -128, 0xffffffffffffffff'0000000002f41522_u128}, // 63
    {Sign::POS, -128, 0xffffffffffffffff'8000000000fc070b_u128}, // 64
    {Sign::NEG, -128, 0xffffffffffffffff'c000000000540259_u128}, // 65
    {Sign::POS, -128, 0xffffffffffffffff'e0000000001c00c8_u128}, // 66
    {Sign::NEG, -128, 0xffffffffffffffff'f000000000095598_u128}, // 67
    {Sign::POS, -128, 0xffffffffffffffff'f800000000031c88_u128}, // 68
    {Sign::NEG, -128, 0xffffffffffffffff'fc00000000010983_u128}, // 69
    {Sign::POS, -128, 0xffffffffffffffff'fe00000000005881_u128}, // 70
    {Sign::NEG, -128, 0xffffffffffffffff'ff00000000001d80_u128}, // 71
    {Sign::POS, -128, 0xffffffffffffffff'ff800000000009d5_u128}, // 72
    {Sign::NEG, -128, 0xffffffffffffffff'ffc0000000000347_u128}, // 73
    {Sign::POS, -128, 0xffffffffffffffff'ffe0000000000118_u128}, // 74
    {Sign::NEG, -128, 0xffffffffffffffff'fff000000000005d_u128}, // 75
    {Sign::POS, -128, 0xffffffffffffffff'fff800000000001f_u128}, // 76
    {Sign::NEG, -128, 0xffffffffffffffff'fffc00000000000a_u128}, // 77
    {Sign::POS, -128, 0xffffffffffffffff'fffe000000000003_u128}, // 78
    {Sign::NEG, -128, 0xffffffffffffffff'ffff000000000001_u128}, // 79
    {Sign::POS, -128, 0xffffffffffffffff'ffff800000000000_u128}, // 80
    {Sign::NEG, -128, 0xffffffffffffffff'ffffc00000000000_u128}, // 81
    {Sign::POS, -128, 0xffffffffffffffff'ffffe00000000000_u128}, // 82
    {Sign::NEG, -128, 0xffffffffffffffff'fffff00000000000_u128}, // 83
    {Sign::POS, -128, 0xffffffffffffffff'fffff80000000000_u128}, // 84
    {Sign::NEG, -128, 0xffffffffffffffff'fffffc0000000000_u128}, // 85
    {Sign::POS, -128, 0xffffffffffffffff'fffffe0000000000_u128}, // 86
    {Sign::NEG, -128, 0xffffffffffffffff'ffffff0000000000_u128}, // 87
    {Sign::POS, -128, 0xffffffffffffffff'ffffff8000000000_u128}, // 88
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffc000000000_u128}, // 89
    {Sign::POS, -128, 0xffffffffffffffff'ffffffe000000000_u128}, // 90
    {Sign::NEG, -128, 0xffffffffffffffff'fffffff000000000_u128}, // 91
    {Sign::POS, -128, 0xffffffffffffffff'fffffff800000000_u128}, // 92
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffc00000000_u128}, // 93
    {Sign::POS, -128, 0xffffffffffffffff'fffffffe00000000_u128}, // 94
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffff00000000_u128}, // 95
    {Sign::POS, -128, 0xffffffffffffffff'ffffffff80000000_u128}, // 96
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffc0000000_u128}, // 97
    {Sign::POS, -128, 0xffffffffffffffff'ffffffffe0000000_u128}, // 98
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffff0000000_u128}, // 99
    {Sign::POS, -128, 0xffffffffffffffff'fffffffff8000000_u128}, // 100
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffffc000000_u128}, // 101
    {Sign::POS, -128, 0xffffffffffffffff'fffffffffe000000_u128}, // 102
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffff000000_u128}, // 103
    {Sign::POS, -128, 0xffffffffffffffff'ffffffffff800000_u128}, // 104
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffffc00000_u128}, // 105
    {Sign::POS, -128, 0xffffffffffffffff'ffffffffffe00000_u128}, // 106
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffffff00000_u128}, // 107
    {Sign::POS, -128, 0xffffffffffffffff'fffffffffff80000_u128}, // 108
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffffffc0000_u128}, // 109
    {Sign::POS, -128, 0xffffffffffffffff'fffffffffffe0000_u128}, // 110
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffffff0000_u128}, // 111
    {Sign::POS, -128, 0xffffffffffffffff'ffffffffffff8000_u128}, // 112
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffffffc000_u128}, // 113
    {Sign::POS, -128, 0xffffffffffffffff'ffffffffffffe000_u128}, // 114
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffffffff000_u128}, // 115
    {Sign::POS, -128, 0xffffffffffffffff'fffffffffffff800_u128}, // 116
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffffffffc00_u128}, // 117
    {Sign::POS, -128, 0xffffffffffffffff'fffffffffffffe00_u128}, // 118
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffffffff00_u128}, // 119
    {Sign::POS, -128, 0xffffffffffffffff'ffffffffffffff80_u128}, // 120
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffffffffc0_u128}, // 121
    {Sign::POS, -128, 0xffffffffffffffff'ffffffffffffffe0_u128}, // 122
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffffffffff0_u128}, // 123
    {Sign::POS, -128, 0xffffffffffffffff'fffffffffffffff8_u128}, // 124
    {Sign::NEG, -128, 0xffffffffffffffff'fffffffffffffffc_u128}, // 125
    {Sign::POS, -128, 0xffffffffffffffff'fffffffffffffffe_u128}, // 126
    {Sign::NEG, -128, 0xffffffffffffffff'ffffffffffffffff_u128}, // 127
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 128
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 129
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 130
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 131
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 132
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 133
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 134
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 135
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 136
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 137
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 138
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 139
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 140
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 141
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 142
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 143
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 144
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 145
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 146
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 147
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 148
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 149
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 150
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 151
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 152
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 153
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 154
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 155
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 156
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 157
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 158
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 159
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 160
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 161
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 162
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 163
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 164
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 165
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 166
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 167
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 168
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 169
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 170
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 171
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 172
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 173
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 174
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 175
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 176
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 177
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 178
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 179
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 180
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 181
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 182
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 183
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 184
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 185
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 186
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 187
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 188
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 189
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 190
    {Sign::NEG, -127, 0x8000000000000000'0000000000000000_u128}, // 191
    {Sign::POS, -127, 0x8000000000000000'0000000000000000_u128}, // 192
};

} // namespace gamma_double_internal
} // namespace math
} // namespace LIBC_NAMESPACE_DECL

#endif // LLVM_LIBC_SRC___SUPPORT_MATH_TGAMMA_CONSTANTS_H
