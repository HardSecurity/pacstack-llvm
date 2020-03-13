//===----------------------------------------------------------------------===//
//
// Author: Hans Liljestrand <hans@liljestrand.dev>
// Copyright (c) 2019 Secure Systems Group, Aalto University https://ssg.aalto.fi/
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "PACStack/AArch64PACStack.h"
#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

#define DEBUG_TYPE "AArch64DummyPA"

// #define PACSTACK_DO_CHECKING
// #define DO_SHORT_DUMMY

using namespace llvm;
using namespace llvm::PACStack;

namespace {

class AArch64DummyPA : public MachineFunctionPass, private AArch64PACStackCommon {
public:
  static char ID;

  AArch64DummyPA() : MachineFunctionPass(ID) {}
  bool runOnMachineFunction(MachineFunction &) override;

private:
  bool convertBasicPAInstr(MachineBasicBlock &MBB, MachineInstr &MI);
  bool convertSpPAInstr(MachineBasicBlock &MBB, MachineInstr &MI);
  bool convertRetPAInstr(MachineBasicBlock &MBB, MachineInstr &MI);
  void insertEmulatedTimings(MachineBasicBlock &MBB, MachineInstr &MI,
                             unsigned dst, unsigned mod);
  void fixupHack(MachineBasicBlock &MBB, MachineInstr &MI);

#ifdef PACSTACK_DO_CHECKING
  bool checkFrameSetup(MachineBasicBlock &MBB, MachineInstr &MI);
  bool checkFrameDestroy(MachineBasicBlock &MBB, MachineInstr &MI);
#endif /* PACSTACK_DO_CHECKING */

  inline bool isPAC(MachineInstr &MI);
};
}

char AArch64DummyPA::ID = 0;

FunctionPass *llvm::createAArch64DummyPA() {
  return new AArch64DummyPA();
}

bool AArch64DummyPA::runOnMachineFunction(MachineFunction &MF) {
  STI = &MF.getSubtarget<AArch64Subtarget>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();

  bool changed = false;

  for (auto &MBB : MF) {
    for (auto MBBI = MBB.begin(), end = MBB.end(); MBBI != end; ) {
      auto &MI = *MBBI++; // update iterator here, since we might remove MI

      switch(MI.getOpcode()) {
        default:
          break;
        case AArch64::PACIA:
        case AArch64::PACIB:
        case AArch64::PACDA:
        case AArch64::PACDB:
        case AArch64::AUTIA:
        case AArch64::AUTIB:
        case AArch64::AUTDA:
        case AArch64::AUTDB:
          changed = convertBasicPAInstr(MBB, MI) | changed;
          break;
        case AArch64::PACIASP:
        case AArch64::PACIBSP:
        case AArch64::AUTIASP:
        case AArch64::AUTIBSP:
          changed = convertSpPAInstr(MBB, MI) | changed;
          break;
        case AArch64::RETAA:
        case AArch64::RETAB:
          changed = convertRetPAInstr(MBB, MI) | changed;
          break;
        case AArch64::PACGA:
        case AArch64::PACDZA:
        case AArch64::PACDZB:
        case AArch64::PACIZA:
        case AArch64::PACIZB:
        case AArch64::PACIA1716:
        case AArch64::PACIAZ:
        case AArch64::PACIB1716:
        case AArch64::PACIBZ:
        case AArch64::AUTDZA:
        case AArch64::AUTDZB:
        case AArch64::AUTIZA:
        case AArch64::AUTIZB:
        case AArch64::AUTIA1716:
        case AArch64::AUTIAZ:
        case AArch64::AUTIB1716:
        case AArch64::AUTIBZ:
          llvm_unreachable("unsupported");
      }
    }
  }

  return changed;
}

bool AArch64DummyPA::convertBasicPAInstr(MachineBasicBlock &MBB, MachineInstr &MI) {
  auto dst = MI.getOperand(0).getReg();
  auto mod = MI.getOperand(1).getReg();

#ifdef PACSTACK_DO_CHECKING
  // These are doing some checking for possible errors due to optimizations
  assert(!MI.getFlag(MachineInstr::FrameSetup) || checkFrameSetup(MBB, MI));
  assert(!MI.getFlag(MachineInstr::FrameDestroy) || checkFrameDestroy(MBB, MI));
#endif /* PACSSTACK_DO_CHECKING */

  insertEmulatedTimings(MBB, MI, dst, mod);

  if (MI.getFlag(MachineInstr::FrameSetup))
    fixupHack(MBB, MI);

  MI.removeFromParent();
  return true;
}

bool AArch64DummyPA::convertSpPAInstr(MachineBasicBlock &MBB, MachineInstr &MI) {
  const auto &DL = MI.getDebugLoc();
  auto dst = MI.getOperand(0).getReg();
  auto mod = AArch64::X15;

  if (!(MI.getFlag(MachineInstr::FrameDestroy) || MI.getFlag(MachineInstr::FrameSetup)))
    llvm_unreachable("dummy conversion of SP variants only supported in FrameSetup or FrameDestroy");

  assert(MI.getOpcode() == AArch64::PACIASP ||
         MI.getOpcode() == AArch64::PACIBSP ||
         MI.getOpcode() == AArch64::AUTIASP ||
         MI.getOpcode() == AArch64::AUTIBSP);

  BuildMI(*MI.getParent(), MI, DL, TII->get(AArch64::ADDXri), mod)
          .addUse(AArch64::SP)
          .addImm(0)
          .addImm(0);

  insertEmulatedTimings(MBB, MI, dst, mod);

  MI.removeFromParent();
  return true;
}

bool AArch64DummyPA::convertRetPAInstr(MachineBasicBlock &MBB, MachineInstr &MI) {
  const auto &DL = MI.getDebugLoc();
  auto dst = AArch64::LR;
  auto mod = AArch64::X15;

  assert(MI.getOpcode() == AArch64::RETAA ||
         MI.getOpcode() == AArch64::RETAB);

  BuildMI(*MI.getParent(), MI, DL, TII->get(AArch64::ADDXri), mod)
          .addUse(AArch64::SP)
          .addImm(0)
          .addImm(0);

  insertEmulatedTimings(MBB, MI, dst, mod);

  BuildMI(MBB, MI, DL, TII->get(AArch64::RET))
          .addReg(AArch64::LR, RegState::Undef);

  MI.removeFromParent();
  return true;
}

void AArch64DummyPA::fixupHack(MachineBasicBlock &MBB,
                               MachineInstr &MI) {
  switch(MI.getOpcode()) {
    default:
      return;
    case AArch64::PACIA:
    case AArch64::PACIB:
    case AArch64::PACDA:
    case AArch64::PACDB:
      for (auto MBBI = MI.getReverseIterator(); MBBI != MBB.rend(); ++MBBI) {
        switch(MBBI->getOpcode()) {
          case AArch64::STRXui:
          case AArch64::STRXpre:
          case AArch64::STPXpre:
          case AArch64::STPXi:
            if (MBBI->killsRegister(AArch64::LR)) {
              MBBI->clearRegisterKills(AArch64::LR, TRI);
              return;
            }
        }
      }
  }
}

void AArch64DummyPA::insertEmulatedTimings(MachineBasicBlock &MBB,
                                           MachineInstr &MI,
                                           unsigned dst, unsigned mod) {
  DebugLoc DL = MI.getDebugLoc();

  const MCInstrDesc &MCID = (isPAC(MI)
                             ? TII->get(AArch64::EORXrs)
                             : TII->get(AArch64::EORXrs));

  if (MI.getFlag(MachineInstr::FrameDestroy)) {
#ifndef DO_SHORT_DUMMY
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(dst).addImm(48)
            .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(52)
            .setMIFlag(MachineInstr::FrameDestroy);
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(56)
            .setMIFlag(MachineInstr::FrameDestroy);
#endif
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(60)
            .setMIFlag(MachineInstr::FrameDestroy);
  } else if (MI.getFlag(MachineInstr::FrameSetup)) {
#ifndef DO_SHORT_DUMMY
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(dst).addImm(48)
            .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(52)
            .setMIFlag(MachineInstr::FrameSetup);
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(56)
            .setMIFlag(MachineInstr::FrameSetup);
#endif
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(60)
            .setMIFlag(MachineInstr::FrameSetup);
  } else  {
#ifndef DO_SHORT_DUMMY
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(dst).addImm(48);
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(52);
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(56);
#endif
    BuildMI(MBB, MI, DL, MCID)
            .addDef(dst)
            .addUse(dst).addReg(mod).addImm(60);
  }
}

inline bool AArch64DummyPA::isPAC(MachineInstr &MI) {
  switch(MI.getOpcode()) {
    default:
      break;
    case AArch64::PACIA:
    case AArch64::PACIB:
    case AArch64::PACDA:
    case AArch64::PACDB:
    case AArch64::PACIASP:
    case AArch64::PACIBSP:
    case AArch64::PACGA:
    case AArch64::PACDZA:
    case AArch64::PACDZB:
    case AArch64::PACIZA:
    case AArch64::PACIZB:
    case AArch64::PACIA1716:
    case AArch64::PACIAZ:
    case AArch64::PACIB1716:
    case AArch64::PACIBZ:
      return true;
  }
  return false;
}

#ifdef PACSTACK_DO_CHECKING
bool AArch64DummyPA::checkFrameSetup(MachineBasicBlock &MBB,
                                     MachineInstr &MI) {
  if (MI.getOperand(0).getReg() != PACStack::maskReg)
    return true; // Only check masking

  assert(!MBB.isLiveIn(PACStack::maskReg));

  constexpr int SET_TO_ZERO = 0;
  constexpr int PAC = 1;
  constexpr int EOR = 2;
  constexpr int RESET_TO_ZERO = 3;

  auto state = SET_TO_ZERO;

  for (auto MBBI = MBB.begin(), end = MBB.end(); MBBI != end; ++MBBI) {

    if (nullptr != MBBI->findRegisterUseOperand(PACStack::maskReg) ||
        nullptr != MBBI->findRegisterDefOperand(PACStack::maskReg)) {
      switch(state) {
        case SET_TO_ZERO:
          assert(MBBI->getOpcode() == AArch64::ORRXrs && "Expecting move");
          assert(
                  MBBI->getOperand(0).getReg() == PACStack::maskReg &&
                  MBBI->getOperand(1).getReg() == AArch64::XZR &&
                  MBBI->getOperand(1).getReg() == AArch64::XZR);
          ++state;
          break;
        case PAC:
          assert(MBBI->getOpcode() == AArch64::PACIA && "Expecting move");
          ++state;
          break;
        case EOR:
          assert(MBBI->getOpcode() == AArch64::EORXrs && "Expecting move");
          ++state;
          break;
        case RESET_TO_ZERO:
          assert(MBBI->getOpcode() == AArch64::ORRXrs && "Expecting move");
          assert(
                  MBBI->getOperand(0).getReg() == PACStack::maskReg &&
                  MBBI->getOperand(1).getReg() == AArch64::XZR &&
                  MBBI->getOperand(1).getReg() == AArch64::XZR);
          return true;
      }
    }
  }
  return false;
}

bool AArch64DummyPA::checkFrameDestroy(MachineBasicBlock &MBB,
                                       MachineInstr &MI) {
  return true;
}
#endif /* PACSTACK_DO_CHECKING */
