#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

// SVF headers
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Util/CommandLine.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"

// standard headers
#include <map>
#include <set>

using namespace llvm;

namespace {

struct SVFPass : public ModulePass {
  static char ID;
  SVFPass() : ModulePass(ID) {}

  static std::string getTypeStr(Type *t) {

    if (!t)
      return "BlackHole";

    std::string str;
    raw_string_ostream rso(str);
    t->print(rso);
    return rso.str();
  }

  static std::unordered_set<SVF::NodeID>
  getAliasCandidatesByRevPts(SVF::PointerAnalysis *pta, SVF::NodeID p,
                             bool include_self = false) {
    std::unordered_set<SVF::NodeID> out;

    // objects
    const auto &pts = pta->getPts(p);

    for (SVF::NodeID o : pts) {

      // pointers
      const auto &rev = pta->getRevPts(o);

      for (SVF::NodeID q : rev) {
        out.insert(q);
      }
    }

    if (!include_self)
      out.erase(p);

    return {out.begin(), out.end()};
  }

  void
  SVFTypeInfer(SVF::SVFIR *svfir, SVF::AndersenWaveDiff *ander,
               std::map<const Value *, std::set<std::string>> &objectsTypes,
               std::map<const Value *, std::set<const Value *>> &aliases) {

    for (auto &pair : objectsTypes) {
      auto Val = pair.first;
      auto typeSet = pair.second;

      SVF::SVFValue *svfVal =
          SVF::LLVMModuleSet::getLLVMModuleSet()->getSVFValue(Val);

      // check if svf tracks this pointer
      if (svfVal && svfir->hasValueNode(svfVal)) {

        auto ptrNodeId = svfir->getValueNode(svfVal);

        auto objAliasIdSet = getAliasCandidatesByRevPts(ander, ptrNodeId);

        for (auto objAliasId : objAliasIdSet) {

          auto *node = svfir->getGNode(objAliasId);

          if (node->hasValue()) {
            auto objAliasSvfVal = node->getValue();
            auto objAliasllvmVal =
                SVF::LLVMModuleSet::getLLVMModuleSet()->getLLVMValue(
                    objAliasSvfVal);

            aliases[Val].insert(objAliasllvmVal);

            auto typestr = getTypeStr(objAliasllvmVal->getType());

            objectsTypes[Val].insert(typestr);
          }
        }

      } else {
        errs() << "[SVF 2.9] This pointer is not tracked: " << *Val << "\n";
      }
    }

    return;
  }

  void dumpResults(std::map<const Value *, std::set<std::string>> objectsTypes,
                   std::map<const Value *, std::set<const Value *>> aliases) {

    for (auto &pair : objectsTypes) {
      auto val = pair.first;
      auto typeSet = pair.second;

      if (typeSet.size() == 1)
        continue;

      errs() << "[LLVM] Type Inconsistent Use" << "\n";
      errs() << "[LLVM] Pointer Value: " << *val << "\n";

      for (auto ptrv : aliases[val]) {
        errs() << "[LLVM] points to: " << *ptrv << "\n";
      }

      for (auto type : typeSet) {
        errs() << "[LLVM] has type: " << type << "\n";
      }
    }
  }

  bool runOnModule(Module &M) override {

    std::map<const Value *, std::set<const Value *>> aliases;
    std::map<const Value *, std::set<std::string>> objectsTypes;

    errs() << "[SVF 2.9] Running Andersen Pointer Analysis..." << "\n";

    // select the pointer analysis to andersen
    SVF::Options::PASelected.parseAndSetValue("ander");

    // disable the stats output
    const_cast<Option<bool> &>(SVF::Options::PStat).setValue(false);

    //  build SVF IR
    SVF::SVFModule *svfMod =
        SVF::LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    SVF::SVFIRBuilder builder(svfMod);
    SVF::SVFIR *svfir = builder.build();

    //  run Andersen's Analysis
    auto ander = SVF::AndersenWaveDiff::createAndersenWaveDiff(svfir);

    // init the object types for every load and store
    for (auto &F : M) {
      for (auto &bb : F) {
        for (auto &I : bb) {
          if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
            auto typestr = getTypeStr(LI->getPointerOperandType());
            objectsTypes[LI].insert(typestr);
          } else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
            auto typestr = getTypeStr(SI->getPointerOperandType());
            objectsTypes[SI].insert(typestr);
          }
        }
      }
    }

    // type inferece for the obj types
    SVFTypeInfer(svfir, ander, objectsTypes, aliases);

    dumpResults(objectsTypes, aliases);

    // cleanup
    errs() << "[SVF 2.9] Cleanning up SVF results..." << "\n";
    SVF::AndersenWaveDiff::releaseAndersenWaveDiff();
    SVF::SVFIR::releaseSVFIR();
    SVF::LLVMModuleSet::releaseLLVMModuleSet();

    return false;
  }
};

} // namespace

// register pass ID
char SVFPass::ID = 0;

// register pass to opt
static RegisterPass<SVFPass> X("svf-pass", "SVF Pass for type inference",
                               /*only looks at CFG=*/false,
                               /*is analysis=*/false);

// register pass to clang
static SVFPass *_PassMaker = NULL;
static RegisterStandardPasses _RegPass1(PassManagerBuilder::EP_OptimizerLast,
                                        [](const PassManagerBuilder &,
                                           legacy::PassManagerBase &PM) {
                                          if (!_PassMaker) {
                                            PM.add(_PassMaker = new SVFPass());
                                          }
                                        }); // ** for -Ox
static RegisterStandardPasses
    _RegPass2(PassManagerBuilder::EP_EnabledOnOptLevel0,
              [](const PassManagerBuilder &, legacy::PassManagerBase &PM) {
                if (!_PassMaker) {
                  PM.add(_PassMaker = new SVFPass());
                }
              }); // ** for -O0
