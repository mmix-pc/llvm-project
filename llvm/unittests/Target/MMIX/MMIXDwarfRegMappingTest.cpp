//===- MMIXDwarfRegMappingTest.cpp - MMIX DWARF register tests ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MMIXMCTargetDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/TargetParser/Triple.h"
#include "gtest/gtest.h"
#include <array>
#include <memory>

using namespace llvm;

namespace {

class MMIXDwarfRegMappingTest : public testing::Test {
protected:
  std::unique_ptr<MCRegisterInfo> MRI{
      createMMIXMCRegisterInfo(Triple("mmix-unknown-elf"))};

  void expectRoundTrip(MCRegister Reg, unsigned DwarfNumber) {
    for (bool IsEH : {false, true}) {
      EXPECT_EQ(MRI->getDwarfRegNum(Reg, IsEH), DwarfNumber);
      EXPECT_EQ(MRI->getLLVMRegNum(DwarfNumber, IsEH), Reg);
    }
  }
};

TEST_F(MMIXDwarfRegMappingTest, GeneralRegistersUseGNUCompatibleNumbers) {
  for (unsigned ArchReg = 0; ArchReg != 256; ++ArchReg) {
    MCRegister Reg = MCRegister::from(MMIX::R0 + ArchReg);
    unsigned DwarfNumber = ArchReg >= 224 ? ArchReg - 224 : ArchReg + 48;
    expectRoundTrip(Reg, DwarfNumber);
  }
}

TEST_F(MMIXDwarfRegMappingTest, ApprovedSpecialRegistersRoundTrip) {
  static constexpr std::array<std::pair<MCRegister, unsigned>, 9> Mappings = {{
      {MMIX::RD, 32},  {MMIX::RE, 33},  {MMIX::RH, 34},
      {MMIX::RJ, 35},  {MMIX::RR, 36},  {MMIX::RO, 38},
      {MMIX::RS, 283}, {MMIX::RG, 291}, {MMIX::RL, 292},
  }};

  for (auto [Reg, DwarfNumber] : Mappings)
    expectRoundTrip(Reg, DwarfNumber);
}

TEST_F(MMIXDwarfRegMappingTest, ExcludedRegistersAndNumbersFailClosed) {
  static constexpr std::array<MCRegister, 23> ExcludedSpecialRegisters = {
      MMIX::RB, MMIX::RM,  MMIX::RBB, MMIX::RC,  MMIX::RN, MMIX::RI,
      MMIX::RT, MMIX::RTT, MMIX::RK,  MMIX::RQ,  MMIX::RU, MMIX::RV,
      MMIX::RA, MMIX::RF,  MMIX::RP,  MMIX::RW,  MMIX::RX, MMIX::RY,
      MMIX::RZ, MMIX::RWW, MMIX::RXX, MMIX::RYY, MMIX::RZZ,
  };

  for (MCRegister Reg : ExcludedSpecialRegisters)
    for (bool IsEH : {false, true})
      EXPECT_EQ(MRI->getDwarfRegNum(Reg, IsEH), -1);

  for (unsigned DwarfNumber : {37, 39, 40, 41, 42, 43, 44, 45, 46, 47, 272,
                               273, 274, 275, 276, 277, 278, 279, 280, 281,
                               282, 284, 285, 286, 287, 288, 289, 290, 293,
                               294, 295, 296, 297, 298, 299, 300, 301, 302,
                               303, 304})
    for (bool IsEH : {false, true})
      EXPECT_EQ(MRI->getLLVMRegNum(DwarfNumber, IsEH), std::nullopt);

  for (bool IsEH : {false, true}) {
    EXPECT_EQ(MRI->getDwarfRegNum(MCRegister(), IsEH), -1);
    EXPECT_EQ(MRI->getDwarfRegNum(
                  MCRegister::from(MCRegister::LastPhysicalReg), IsEH),
              -1);
    EXPECT_EQ(MRI->getLLVMRegNum(305, IsEH), std::nullopt);
  }
}

} // namespace
