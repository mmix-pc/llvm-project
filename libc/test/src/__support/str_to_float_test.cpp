//===-- Unittests for str_to_float<float> ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "src/__support/macros/config.h"
#include "str_to_fp_test.h"

namespace LIBC_NAMESPACE_DECL {

using LlvmLibcStrToFltTest = LlvmLibcStrToFloatTest<float>;

TEST_F(LlvmLibcStrToFltTest, ClingerFastPathFloat32Simple) {
  clinger_fast_path_test(123, 0, 0xf60000, 133);
  clinger_fast_path_test(1234567, 1, 0xbc6146, 150);
  clinger_fast_path_test(12345, -5, 0xfcd35b, 123);
}

TEST_F(LlvmLibcStrToFltTest, ClingerFastPathFloat32ExtendedExp) {
  clinger_fast_path_test(1, 15, 0xe35fa9, 176);
  clinger_fast_path_test(1, 17, 0xb1a2bc, 183);
  clinger_fast_path_fails_test(10, 17);
  clinger_fast_path_fails_test(1, 50);
}

TEST_F(LlvmLibcStrToFltTest, ClingerFastPathFloat32NegativeExp) {
  clinger_fast_path_test(1, -5, 0xa7c5ac, 110);
  clinger_fast_path_test(1, -10, 0xdbe6ff, 93);
  clinger_fast_path_fails_test(1, -15);
}

// Check the fallback states for the algorithm:
TEST_F(LlvmLibcStrToFltTest, EiselLemireFallbackStates) {
  // This number can't be evaluated by Eisel-Lemire since it's exactly 1024 away
  // from both of its closest floating point approximations
  // (12345678901234548736 and 12345678901234550784)
  ASSERT_FALSE(internal::eisel_lemire<float>({20040229, 0}).has_value());
}

TEST_F(LlvmLibcStrToFltTest, SimpleDecimalConversion32SpecificFailures) {
  simple_decimal_conversion_test(
      "1.4012984643248170709237295832899161312802619418765e-45", 0x1, 0,
      ERANGE);
  simple_decimal_conversion_test(
      "7."
      "006492321624085354618647916449580656401309709382578858785341419448955413"
      "42930300743319094181060791015625e-46",
      0x0, 0, ERANGE);
}

TEST(LlvmLibcStrToFltTest, SimpleDecimalConversionExtraTypes) {
  uint32_t float_output_mantissa = 0;
  uint32_t output_exp2 = 0;

  auto float_result =
      internal::simple_decimal_conversion<float>("123456789012345678900");
  float_output_mantissa = float_result.num.mantissa;
  output_exp2 = static_cast<uint32_t>(float_result.num.exponent);
  EXPECT_EQ(float_output_mantissa, uint32_t(0xd629d4));
  EXPECT_EQ(output_exp2, uint32_t(193));
  EXPECT_EQ(float_result.error, 0);
}

TEST(LlvmLibcStrToFltTest, ExplicitRadixPreservesDefaultGrammar) {
  internal::DecimalSeparator<char> comma(",", 1);
  auto parsed = internal::strtofloatingpoint<float>(" -1,25rest", comma);
  EXPECT_EQ(fputil::FPBits<float>(parsed.value).uintval(),
            uint32_t(0xbfa00000));
  EXPECT_EQ(parsed.parsed_len, ptrdiff_t(6));
  EXPECT_EQ(parsed.error, 0);
  auto ordinary = internal::strtofloatingpoint<float>(" -1,25rest");
  EXPECT_EQ(fputil::FPBits<float>(ordinary.value).uintval(),
            uint32_t(0xbf800000));
  EXPECT_EQ(ordinary.parsed_len, ptrdiff_t(3));
  auto hexadecimal = internal::strtofloatingpoint<float>("0x1,8p+1!", comma);
  EXPECT_EQ(fputil::FPBits<float>(hexadecimal.value).uintval(),
            uint32_t(0x40400000));
  EXPECT_EQ(hexadecimal.parsed_len, ptrdiff_t(8));
  EXPECT_EQ(fputil::FPBits<float>(
                internal::strtofloatingpoint<float>("0x,8p+1", comma).value)
                .uintval(),
            uint32_t(0x3f800000));
  EXPECT_EQ(internal::strtofloatingpoint<float>(",e10", comma).parsed_len,
            ptrdiff_t(0));
  EXPECT_EQ(internal::strtofloatingpoint<float>("1,2,3", comma).parsed_len,
            ptrdiff_t(3));
  internal::DecimalSeparator<char> two("::", 2);
  auto sequence = internal::strtofloatingpoint<float>("1::25", two);
  EXPECT_EQ(fputil::FPBits<float>(sequence.value).uintval(),
            uint32_t(0x3fa00000));
  EXPECT_EQ(sequence.parsed_len, ptrdiff_t(5));
  EXPECT_EQ(internal::strtofloatingpoint<float>("1:", two).parsed_len,
            ptrdiff_t(1));
  EXPECT_EQ(fputil::FPBits<float>(
                internal::strtofloatingpoint<float>("0x::8p1", two).value)
                .uintval(),
            uint32_t(0x3f800000));
  internal::DecimalSeparator<wchar_t> wide(L"\u066b", 1);
  auto wide_result = internal::strtofloatingpoint<float>(L"1\u066b25", wide);
  EXPECT_EQ(fputil::FPBits<float>(wide_result.value).uintval(),
            uint32_t(0x3fa00000));
  EXPECT_EQ(wide_result.parsed_len, ptrdiff_t(4));
}

TEST(LlvmLibcStrToFltTest, ExplicitRadixReachesPrecisionFallback) {
  const char original[] =
      "1.4012984643248170709237295832899161312802619418765e-45";
  const char localized[] =
      "1,4012984643248170709237295832899161312802619418765e-45";
  internal::DecimalSeparator<char> comma(",", 1);
  const internal::RoundDirection rounds[] = {internal::RoundDirection::Nearest,
                                             internal::RoundDirection::Up,
                                             internal::RoundDirection::Down};
  for (auto round : rounds) {
    auto expected = internal::simple_decimal_conversion<float>(
        original, sizeof(original) - 1, round);
    auto result = internal::simple_decimal_conversion<float>(
        localized, sizeof(localized) - 1, round, comma);
    EXPECT_EQ(result.num.mantissa, expected.num.mantissa);
    EXPECT_EQ(result.num.exponent, expected.num.exponent);
    EXPECT_EQ(result.error, expected.error);
  }
  auto result = internal::strtofloatingpoint<float>(localized, comma);
  EXPECT_EQ(fputil::FPBits<float>(result.value).uintval(), uint32_t(1));
  EXPECT_EQ(result.error, ERANGE);
  EXPECT_EQ(result.parsed_len, ptrdiff_t(sizeof(localized) - 1));
}

} // namespace LIBC_NAMESPACE_DECL
