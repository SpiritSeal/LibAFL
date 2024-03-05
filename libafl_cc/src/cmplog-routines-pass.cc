/*
   american fuzzy lop++ - LLVM CmpLog instrumentation
   --------------------------------------------------

   Written by Andrea Fioraldi <andreafioraldi@gmail.com>

   Copyright 2015, 2016 Google Inc. All rights reserved.
   Copyright 2019-2020 AFLplusplus Project. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

*/

#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
  #include <unistd.h>
  #include <sys/time.h>
#endif

#include <list>
#include <string>
#include <fstream>

#include "common-llvm.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/ValueTracking.h"

#if LLVM_VERSION_MAJOR > 3 || \
    (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR > 4)
  #include "llvm/IR/Verifier.h"
  #include "llvm/IR/DebugInfo.h"
#else
  #include "llvm/Analysis/Verifier.h"
  #include "llvm/DebugInfo.h"
  #define nullptr 0
#endif

#include <set>

using namespace llvm;
static cl::opt<bool> CmplogExtended("cmplog_routines_extended",
                                    cl::desc("Uses extended header"),
                                    cl::init(false), cl::NotHidden);
namespace {

#if USE_NEW_PM
class CmpLogRoutines : public PassInfoMixin<CmpLogRoutines> {
 public:
  CmpLogRoutines() {
#else

class CmpLogRoutines : public ModulePass {
 public:
  static char ID;
  CmpLogRoutines() : ModulePass(ID) {
#endif
  }

#if USE_NEW_PM
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
#else
  bool runOnModule(Module &M) override;

  #if LLVM_VERSION_MAJOR < 4
  const char *getPassName() const override {
  #else
  StringRef getPassName() const override {
  #endif
    return "cmplog routines";
  }
#endif

 private:
  bool hookRtns(Module &M);
};

}  // namespace

#if USE_NEW_PM
extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "CmpLogRoutines", "v0.1",
          [](PassBuilder &PB) {
  #if LLVM_VERSION_MAJOR <= 13
            using OptimizationLevel = typename PassBuilder::OptimizationLevel;
  #endif
            PB.registerOptimizerLastEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel OL) {
                  MPM.addPass(CmpLogRoutines());
                });
          }};
}
#else
char CmpLogRoutines::ID = 0;
#endif
#include <iostream>

bool CmpLogRoutines::hookRtns(Module &M) {
  std::vector<CallInst *> calls, llvmStdStd, llvmStdC, gccStdStd, gccStdC,
      Memcmp, Strcmp, Strncmp;
  LLVMContext &C = M.getContext();

  Type *VoidTy = Type::getVoidTy(C);
  // PointerType *VoidPtrTy = PointerType::get(VoidTy, 0);
  IntegerType *Int8Ty = IntegerType::getInt8Ty(C);
  IntegerType *Int64Ty = IntegerType::getInt64Ty(C);
  IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
  PointerType *i8PtrTy = PointerType::get(Int8Ty, 0);

  FunctionCallee cmplogHookFn;
  FunctionCallee cmplogLlvmStdStd;
  FunctionCallee cmplogLlvmStdC;
  FunctionCallee cmplogGccStdStd;
  FunctionCallee cmplogGccStdC;
  FunctionCallee cmplogHookFnN;
  FunctionCallee cmplogHookFnStrN;
  FunctionCallee cmplogHookFnStr;

  if (CmplogExtended) {
    cmplogHookFn = M.getOrInsertFunction("__cmplog_rtn_hook_extended", VoidTy,
                                         i8PtrTy, i8PtrTy);
  } else {
    cmplogHookFn =
        M.getOrInsertFunction("__cmplog_rtn_hook", VoidTy, i8PtrTy, i8PtrTy);
  }

  if (CmplogExtended) {
    cmplogLlvmStdStd =
        M.getOrInsertFunction("__cmplog_rtn_llvm_stdstring_stdstring_extended",
                              VoidTy, i8PtrTy, i8PtrTy);
  } else {
    cmplogLlvmStdStd = M.getOrInsertFunction(
        "__cmplog_rtn_llvm_stdstring_stdstring", VoidTy, i8PtrTy, i8PtrTy);
  }

  if (CmplogExtended) {
    cmplogLlvmStdC =
        M.getOrInsertFunction("__cmplog_rtn_llvm_stdstring_cstring_extended",
                              VoidTy, i8PtrTy, i8PtrTy);
  } else {
    cmplogLlvmStdC = M.getOrInsertFunction(
        "__cmplog_rtn_llvm_stdstring_cstring", VoidTy, i8PtrTy, i8PtrTy);
  }

  if (CmplogExtended) {
    cmplogGccStdStd =
        M.getOrInsertFunction("__cmplog_rtn_gcc_stdstring_stdstring_extended",
                              VoidTy, i8PtrTy, i8PtrTy);
  } else {
    cmplogGccStdStd = M.getOrInsertFunction(
        "__cmplog_rtn_gcc_stdstring_stdstring", VoidTy, i8PtrTy, i8PtrTy);
  }

  if (CmplogExtended) {
    cmplogGccStdC =
        M.getOrInsertFunction("__cmplog_rtn_gcc_stdstring_cstring_extended",
                              VoidTy, i8PtrTy, i8PtrTy);
  } else {
    cmplogGccStdC = M.getOrInsertFunction("__cmplog_rtn_gcc_stdstring_cstring",
                                          VoidTy, i8PtrTy, i8PtrTy);
  }

  if (CmplogExtended) {
    cmplogHookFnN = M.getOrInsertFunction("__cmplog_rtn_hook_n_extended",
                                          VoidTy, i8PtrTy, i8PtrTy, Int64Ty);
  } else {
    cmplogHookFnN = M.getOrInsertFunction("__cmplog_rtn_hook_n", VoidTy,
                                          i8PtrTy, i8PtrTy, Int64Ty);
  }

  if (CmplogExtended) {
    cmplogHookFnStrN = M.getOrInsertFunction("__cmplog_rtn_hook_strn_extended",
                                             VoidTy, i8PtrTy, i8PtrTy, Int64Ty);
  } else {
    cmplogHookFnStrN = M.getOrInsertFunction("__cmplog_rtn_hook_strn", VoidTy,
                                             i8PtrTy, i8PtrTy, Int64Ty);
  }

  if (CmplogExtended) {
    cmplogHookFnStr = M.getOrInsertFunction("__cmplog_rtn_hook_str_extended",
                                            VoidTy, i8PtrTy, i8PtrTy);
  } else {
    cmplogHookFnStr = M.getOrInsertFunction("__cmplog_rtn_hook_str", VoidTy,
                                            i8PtrTy, i8PtrTy);
  }

  /* iterate over all functions, bbs and instruction and add suitable calls */
  for (auto &F : M) {
    if (isIgnoreFunction(&F)) { continue; }

    for (auto &BB : F) {
      for (auto &IN : BB) {
        CallInst *callInst = nullptr;

        if ((callInst = dyn_cast<CallInst>(&IN))) {
          Function *Callee = callInst->getCalledFunction();
          if (!Callee) { continue; }
          if (callInst->getCallingConv() != llvm::CallingConv::C) { continue; }

          FunctionType *FT = Callee->getFunctionType();
          std::string   FuncName = Callee->getName().str();

          bool isPtrRtn = FT->getNumParams() >= 2 &&
                          !FT->getReturnType()->isVoidTy() &&
                          FT->getParamType(0) == FT->getParamType(1) &&
                          FT->getParamType(0)->isPointerTy();

          bool isPtrRtnN = FT->getNumParams() >= 3 &&
                           !FT->getReturnType()->isVoidTy() &&
                           FT->getParamType(0) == FT->getParamType(1) &&
                           FT->getParamType(0)->isPointerTy() &&
                           FT->getParamType(2)->isIntegerTy();
          if (isPtrRtnN) {
            auto intTyOp =
                dyn_cast<IntegerType>(callInst->getArgOperand(2)->getType());
            if (intTyOp) {
              if (intTyOp->getBitWidth() != 32 &&
                  intTyOp->getBitWidth() != 64) {
                isPtrRtnN = false;
              }
            }
          }

          bool isMemcmp =
              (!FuncName.compare("memcmp") || !FuncName.compare("bcmp") ||
               !FuncName.compare("CRYPTO_memcmp") ||
               !FuncName.compare("OPENSSL_memcmp") ||
               !FuncName.compare("memcmp_const_time") ||
               !FuncName.compare("memcmpct"));
          isMemcmp &= FT->getNumParams() == 3 &&
                      FT->getReturnType()->isIntegerTy(32) &&
                      FT->getParamType(0)->isPointerTy() &&
                      FT->getParamType(1)->isPointerTy() &&
                      FT->getParamType(2)->isIntegerTy();

          bool isStrcmp =
              (!FuncName.compare("strcmp") || !FuncName.compare("xmlStrcmp") ||
               !FuncName.compare("xmlStrEqual") ||
               !FuncName.compare("g_strcmp0") ||
               !FuncName.compare("curl_strequal") ||
               !FuncName.compare("strcsequal") ||
               !FuncName.compare("strcasecmp") ||
               !FuncName.compare("stricmp") ||
               !FuncName.compare("ap_cstr_casecmp") ||
               !FuncName.compare("OPENSSL_strcasecmp") ||
               !FuncName.compare("xmlStrcasecmp") ||
               !FuncName.compare("g_strcasecmp") ||
               !FuncName.compare("g_ascii_strcasecmp") ||
               !FuncName.compare("Curl_strcasecompare") ||
               !FuncName.compare("Curl_safe_strcasecompare") ||
               !FuncName.compare("cmsstrcasecmp") ||
               !FuncName.compare("strstr") ||
               !FuncName.compare("g_strstr_len") ||
               !FuncName.compare("ap_strcasestr") ||
               !FuncName.compare("xmlStrstr") ||
               !FuncName.compare("xmlStrcasestr") ||
               !FuncName.compare("g_str_has_prefix") ||
               !FuncName.compare("g_str_has_suffix"));
          isStrcmp &=
              FT->getNumParams() == 2 && FT->getReturnType()->isIntegerTy(32) &&
              FT->getParamType(0) == FT->getParamType(1) &&
              FT->getParamType(0) ==
                  IntegerType::getInt8Ty(M.getContext())->getPointerTo(0);

          bool isStrncmp = (!FuncName.compare("strncmp") ||
                            !FuncName.compare("xmlStrncmp") ||
                            !FuncName.compare("curl_strnequal") ||
                            !FuncName.compare("strncasecmp") ||
                            !FuncName.compare("strnicmp") ||
                            !FuncName.compare("ap_cstr_casecmpn") ||
                            !FuncName.compare("OPENSSL_strncasecmp") ||
                            !FuncName.compare("xmlStrncasecmp") ||
                            !FuncName.compare("g_ascii_strncasecmp") ||
                            !FuncName.compare("Curl_strncasecompare") ||
                            !FuncName.compare("g_strncasecmp"));
          isStrncmp &=
              FT->getNumParams() == 3 && FT->getReturnType()->isIntegerTy(32) &&
              FT->getParamType(0) == FT->getParamType(1) &&
              FT->getParamType(0) ==
                  IntegerType::getInt8Ty(M.getContext())->getPointerTo(0) &&
              FT->getParamType(2)->isIntegerTy();

          bool isGccStdStringStdString =
              Callee->getName().find("__is_charIT_EE7__value") !=
                  std::string::npos &&
              Callee->getName().find(
                  "St7__cxx1112basic_stringIS2_St11char_traits") !=
                  std::string::npos &&
              FT->getNumParams() >= 2 &&
              FT->getParamType(0) == FT->getParamType(1) &&
              FT->getParamType(0)->isPointerTy();

          bool isGccStdStringCString =
              Callee->getName().find(
                  "St7__cxx1112basic_stringIcSt11char_"
                  "traitsIcESaIcEE7compareEPK") != std::string::npos &&
              FT->getNumParams() >= 2 && FT->getParamType(0)->isPointerTy() &&
              FT->getParamType(1)->isPointerTy();

          bool isLlvmStdStringStdString =
              Callee->getName().find("_ZNSt3__1eqI") != std::string::npos &&
              Callee->getName().find("_12basic_stringI") != std::string::npos &&
              Callee->getName().find("_11char_traits") != std::string::npos &&
              FT->getNumParams() >= 2 && FT->getParamType(0)->isPointerTy() &&
              FT->getParamType(1)->isPointerTy();

          bool isLlvmStdStringCString =
              Callee->getName().find("_ZNSt3__1eqI") != std::string::npos &&
              Callee->getName().find("_12basic_stringI") != std::string::npos &&
              FT->getNumParams() >= 2 && FT->getParamType(0)->isPointerTy() &&
              FT->getParamType(1)->isPointerTy();

          /*
                    {

                       fprintf(stderr, "F:%s C:%s argc:%u\n",
                       F.getName().str().c_str(),
             Callee->getName().str().c_str(), FT->getNumParams());
                       fprintf(stderr, "ptr0:%u ptr1:%u ptr2:%u\n",
                              FT->getParamType(0)->isPointerTy(),
                              FT->getParamType(1)->isPointerTy(),
                              FT->getNumParams() > 2 ?
             FT->getParamType(2)->isPointerTy() : 22 );

                    }

          */

          if (isGccStdStringCString || isGccStdStringStdString ||
              isLlvmStdStringStdString || isLlvmStdStringCString || isMemcmp ||
              isStrcmp || isStrncmp) {
            isPtrRtnN = isPtrRtn = false;
          }

          if (isPtrRtnN) { isPtrRtn = false; }

          if (isPtrRtn) { calls.push_back(callInst); }
          if (isMemcmp || isPtrRtnN) { Memcmp.push_back(callInst); }
          if (isStrcmp) { Strcmp.push_back(callInst); }
          if (isStrncmp) { Strncmp.push_back(callInst); }
          if (isGccStdStringStdString) { gccStdStd.push_back(callInst); }
          if (isGccStdStringCString) { gccStdC.push_back(callInst); }
          if (isLlvmStdStringStdString) { llvmStdStd.push_back(callInst); }
          if (isLlvmStdStringCString) { llvmStdC.push_back(callInst); }
        }
      }
    }
  }

  if (!calls.size() && !gccStdStd.size() && !gccStdC.size() &&
      !llvmStdStd.size() && !llvmStdC.size() && !Memcmp.size() &&
      Strcmp.size() && Strncmp.size())
    return false;

  for (auto &callInst : calls) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);

    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);

    IRB.CreateCall(cmplogHookFn, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  for (auto &callInst : Memcmp) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1),
          *v3P = callInst->getArgOperand(2);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);

    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    Value               *v3Pbitcast = IRB.CreateBitCast(
        v3P, IntegerType::get(C, v3P->getType()->getPrimitiveSizeInBits()));
    Value *v3Pcasted =
        IRB.CreateIntCast(v3Pbitcast, IntegerType::get(C, 64), false);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);
    args.push_back(v3Pcasted);

    IRB.CreateCall(cmplogHookFnN, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  for (auto &callInst : Strcmp) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);
    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);

    IRB.CreateCall(cmplogHookFnStr, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  for (auto &callInst : Strncmp) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1),
          *v3P = callInst->getArgOperand(2);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);
    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    Value               *v3Pbitcast = IRB.CreateBitCast(
        v3P, IntegerType::get(C, v3P->getType()->getPrimitiveSizeInBits()));
    Value *v3Pcasted =
        IRB.CreateIntCast(v3Pbitcast, IntegerType::get(C, 64), false);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);
    args.push_back(v3Pcasted);

    IRB.CreateCall(cmplogHookFnStrN, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  for (auto &callInst : gccStdStd) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);

    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);

    IRB.CreateCall(cmplogGccStdStd, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  for (auto &callInst : gccStdC) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);

    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);

    IRB.CreateCall(cmplogGccStdC, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  for (auto &callInst : llvmStdStd) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);

    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);

    IRB.CreateCall(cmplogLlvmStdStd, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  for (auto &callInst : llvmStdC) {
    Value *v1P = callInst->getArgOperand(0), *v2P = callInst->getArgOperand(1);

    IRBuilder<> IRB(callInst->getParent());
    IRB.SetInsertPoint(callInst);

    std::vector<Value *> args;
    Value               *v1Pcasted = IRB.CreatePointerCast(v1P, i8PtrTy);
    Value               *v2Pcasted = IRB.CreatePointerCast(v2P, i8PtrTy);
    args.push_back(v1Pcasted);
    args.push_back(v2Pcasted);

    IRB.CreateCall(cmplogLlvmStdC, args);

    // errs() << callInst->getCalledFunction()->getName() << "\n";
  }

  return true;
}

#if USE_NEW_PM
PreservedAnalyses CmpLogRoutines::run(Module &M, ModuleAnalysisManager &MAM) {
#else
bool CmpLogRoutines::runOnModule(Module &M) {
#endif
  hookRtns(M);

#if USE_NEW_PM
  auto PA = PreservedAnalyses::all();
#endif
  verifyModule(M);

#if USE_NEW_PM
  return PA;
#else
  return true;
#endif
}

#if USE_NEW_PM
#else
static void registerCmpLogRoutinesPass(const PassManagerBuilder &,
                                       legacy::PassManagerBase &PM) {
  auto p = new CmpLogRoutines();
  PM.add(p);
}

static RegisterStandardPasses RegisterCmpLogRoutinesPass(
    PassManagerBuilder::EP_OptimizerLast, registerCmpLogRoutinesPass);

static RegisterStandardPasses RegisterCmpLogRoutinesPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerCmpLogRoutinesPass);

static RegisterStandardPasses RegisterCmpLogRoutinesPassLTO(
    PassManagerBuilder::EP_FullLinkTimeOptimizationLast,
    registerCmpLogRoutinesPass);

#endif