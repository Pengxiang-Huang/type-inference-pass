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

struct InstTypeVisitor : InstVisitor<InstTypeVisitor> {

  std::map<const Value *, std::set<Type *>> &objectsTypes;

  InstTypeVisitor(std::map<const Value *, std::set<Type *>> &objectsTypes)
      : objectsTypes(objectsTypes) {}

  // unhandled instruction
  void visitInstruction(Instruction &I) {
    errs() << "unhandled visiting: " << I << "\n";
  }

  void visitLoadInst(LoadInst &I) {
    auto ty = I.getType();
    objectsTypes[&I].insert(ty);
  }

  void visitStoreInst(StoreInst &I) {
    auto ty = I.getValueOperand()->getType();
    objectsTypes[&I].insert(ty);
  }
};

struct SVFPass : public ModulePass {
  static char ID;
  SVFPass() : ModulePass(ID) {}

  static std::string getTypeStr(Type *t) {

    if (!t) {
      errs() << "type can not be empty"
             << "\n";
      abort();
    }

    std::string str;
    raw_string_ostream rso(str);
    t->print(rso);
    return rso.str();
  }

  static void
  getAliases(const Value *Val,
             std::map<const Value *, std::set<const Value *>> &aliases,
             SVF::SVFIR *svfir, SVF::PointerAnalysis *pta, SVF::NodeID p) {

    auto validPtrsNodes = svfir->getAllValidPtrs();

    for (auto nodeid : validPtrsNodes) {

      // if (svfir->isBlkPtr(nodeid)) errs() << "catch a black hole ptr\n";

      auto node = svfir->getGNode(nodeid);

      if (!node->hasValue())
        continue;

      auto nodeValue = node->getValue();

      auto llvmVal =
          SVF::LLVMModuleSet::getLLVMModuleSet()->getLLVMValue(nodeValue);

      auto aliasResult = pta->alias(p, nodeid);

      if (aliasResult != SVF::NoAlias) {
        aliases[Val].insert(llvmVal);
      }
    }
  }

  void SVFGetAlias(SVF::SVFIR *svfir, SVF::AndersenWaveDiff *ander,
                   std::set<const Value *> &targets,
                   std::set<const Value *> &typeUnknown,
                   std::map<const Value *, std::set<const Value *>> &aliases) {

    errs() << "[SVF 2.9] Running SVF to get aliases"
           << "\n";

    for (auto Val : targets) {

      // get its svf value
      SVF::SVFValue *svfVal =
          SVF::LLVMModuleSet::getLLVMModuleSet()->getSVFValue(Val);

      // check if svf tracks this pointer
      if (svfVal && svfir->hasValueNode(svfVal)) {

        // check if its a black hole or null pointer
        if (svfVal->isblackHole() || svfVal->isNullPtr()) {

          typeUnknown.insert(Val);
          errs() << "unknown alias for val: " << *Val << "\n";

          // not tracked further
          continue;
        }

        // get svf node id to query all the aliases
        auto ptrNodeId = svfir->getValueNode(svfVal);

        getAliases(Val, aliases, svfir, ander, ptrNodeId);

      } else {
        errs() << "[SVF 2.9] This pointer is not tracked: " << *Val << "\n";
      }
    }
  }

  void TypeInfer(std::map<const Value *, std::set<const Value *>> &aliases,
                 std::map<const Value *, std::set<Type *>> &objectsTypes) {

    // InstTypeVisitor visitor(objectsTypes);

    for (auto &pair : aliases) {

      auto v = pair.first;
      auto aliasSet = pair.second;

      for (auto &aliasV : aliasSet) {

        auto constInst = static_cast<const Instruction *>(aliasV);

        // cast to non const for the llvm visitor
        auto inst = const_cast<Instruction *>(constInst);

        // visitor.visit(inst)	;
        //
        auto loadInst = dyn_cast<LoadInst>(inst);
        auto storeInst = dyn_cast<StoreInst>(constInst);

        // now only check the use of load or store
        if (loadInst) {
          objectsTypes[v].insert(loadInst->getType());
        }
        if (storeInst) {
          objectsTypes[v].insert(storeInst->getValueOperand()->getType());
        }
      }
    }

    return;
  }

  void dumpResults(std::map<const Value *, std::set<Type *>> &objectsTypes,
                   std::map<const Value *, std::set<const Value *>> aliases,
                   std::set<const Value *> typeUnknown) {

    errs() << "Printing aliases..."
           << "\n";
    for (auto &pair : aliases) {
      auto val = pair.first;
      auto aliasSet = pair.second;

      errs() << "Value: " << *val << " has alias: "
             << "\n";

      for (auto v : aliasSet) {
        errs() << *v << "\n";
      }
    }

    errs() << "Type Unknowns: " << "\n";
    for (auto &v : typeUnknown) {
      errs() << v << "\n";
    }

    errs() << "objectsType size: " << objectsTypes.size() << "\n";

    for (auto &pair : objectsTypes) {
      auto val = pair.first;
      auto typeSet = pair.second;

      if (typeSet.size() == 1)
        continue;

      errs() << "Type Inconsistent Use"
             << "\n";
      errs() << "Pointer Value: " << *val << "\n";

      // for (auto ptrv : aliases[val]) {
      //   errs() << "points to: " << *ptrv << "\n";
      // }

      for (auto type : typeSet) {
        errs() << "has type: " << getTypeStr(type) << "\n";
      }
    }
  }

  bool runOnModule(Module &M) override {

    std::set<const Value *> targets;
    std::set<const Value *> typeUnknown;
    std::map<const Value *, std::set<const Value *>> aliases;
    std::map<const Value *, std::set<Type *>> objectsTypes;

    errs() << "[SVF 2.9] Running Andersen Pointer Analysis..."
           << "\n";

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

    // we are interested in the type usage consistency for every store
    for (auto &F : M) {
      for (auto &bb : F) {
        for (auto &I : bb) {
          // if(LoadInst *LI = dyn_cast<LoadInst>(&I)){
          // }
          if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
            auto ptrinst = SI->getPointerOperand();
            targets.insert(ptrinst);
          }
        }
      }
    }

    // use SVF to get the aliases
    SVFGetAlias(svfir, ander, targets, typeUnknown, aliases);

    // type inferece for the obj types
    TypeInfer(aliases, objectsTypes);

    dumpResults(objectsTypes, aliases, typeUnknown);

    // cleanup
    errs() << "[SVF 2.9] Cleanning up SVF results..."
           << "\n";
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
