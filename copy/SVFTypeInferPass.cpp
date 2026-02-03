#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"

// SVF headers
#include "SVF-LLVM/LLVMModule.h"
#include "SVF-LLVM/SVFIRBuilder.h"
#include "Util/Options.h"
#include "WPA/Andersen.h"

// standard headers
#include <map>
#include <set>

using namespace llvm;
using namespace SVF;

namespace {

struct SVFPass : public ModulePass {
  static char ID;
  SVFPass() : ModulePass(ID) {}

  static std::string getTypeStr(Type *t) {
    std::string str;
    raw_string_ostream rso(str);
    t->print(rso);
    return rso.str();
  }




  bool runOnModule(Module &M) override {

    errs() << "[SVF 2.9] Running Andersen Pointer Analysis...\n";

		SVFStat::printGeneralStats = false;    
		// SVF::Options::PStat.setValue(false);

    //  build SVF IR
    SVFModule *svfMod = LLVMModuleSet::getLLVMModuleSet()->buildSVFModule(M);
    SVFIRBuilder builder(svfMod);
    SVFIR *svfir = builder.build();


    //  run Andersen's Analysis
    auto ander = AndersenWaveDiff::createAndersenWaveDiff(svfir);

    std::map<NodeID, std::set<std::string>> objectTypes;

    // Map: Memory Object ID -> List of instructions accessing it (for
    // reporting)
    std::map<NodeID, std::vector<std::string>> objectUsers;

    for (auto &F : M) {
      if (F.isDeclaration())
        continue;

      // iterate the instructions
      for (auto &bb : F) {
        for (auto &I : bb) {

          auto inst = &I;
          Type *accessedType = nullptr;
          Value *pointerOp = nullptr;

          if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
            accessedType = LI->getType();
            pointerOp = LI->getPointerOperand();
          }

          else if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
            accessedType = SI->getValueOperand()->getType();
            pointerOp = SI->getPointerOperand();
          }

          if (accessedType && pointerOp) {

            if (auto instOp = dyn_cast<Instruction>(pointerOp)) {

              SVFValue *svfVal =
                  LLVMModuleSet::getLLVMModuleSet()->getSVFValue(instOp);

              // check if SVF tracks this pointer
              if (svfVal && svfir->hasValueNode(svfVal)) {
                NodeID ptrNodeId = svfir->getValueNode(svfVal);

                // Resolve Alias: What objects does this pointer point to?
                const PointsTo &pts = ander->getPts(ptrNodeId);

                for (NodeID objID : pts) {
                  // Skip internal dummy nodes (BlackHole, Null, etc.)
                  if (!svfir->getGNode(objID)->hasValue())
                    continue;

                  // Record the Type
                  std::string tStr = getTypeStr(accessedType);
                  objectTypes[objID].insert(tStr);

                  // Record the Source Location for the report
                  std::string loc =
                      F.getName().str() + ": " + inst->getName().str();
                  if (DILocation *Loc = inst->getDebugLoc()) {
                    loc += " (Line " + std::to_string(Loc->getLine()) + ")";
                  }
                  objectUsers[objID].push_back(loc + " [" + tStr + "]");
                }
              }
            }
          }
        }
      }
    }

    for (auto const &entry : objectTypes) {
      NodeID objID = entry.first;
      auto types = entry.second;

      // DSA Logic: If an object is accessed as >1 distinct type, it is
      // "Collapsed"
      if (types.size() > 1) {
        errs() << "--------------------------------------------------\n";
        errs() << "[VIOLATION] Memory Object ID " << objID
               << " is used inconsistently!\n";

        // Print what the object actually is (Allocation site)
        PAGNode *node = svfir->getGNode(objID);
        const Value *v =
            LLVMModuleSet::getLLVMModuleSet()->getLLVMValue(node->getValue());
        if (v) {
          errs() << "  Object Declared as: ";
          if (v->hasName())
            errs() << "%" << v->getName() << " ";
          errs() << *v << "\n";
        }

        errs() << "  Conflicting Types Used: { ";
        for (const auto &t : types)
          errs() << t << " ";
        errs() << "}\n";

        errs() << "  Access History:\n";
        for (const auto &user : objectUsers[objID]) {
          errs() << "    -> " << user << "\n";
        }
      }
    }

    // cleanup
    AndersenWaveDiff::releaseAndersenWaveDiff();
    SVFIR::releaseSVFIR();
    LLVMModuleSet::releaseLLVMModuleSet();

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
