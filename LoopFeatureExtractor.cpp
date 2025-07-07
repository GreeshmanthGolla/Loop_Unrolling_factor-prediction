#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <memory>
#include <set>

using namespace llvm;

namespace {
struct LoopFeatureExtractor : public PassInfoMixin<LoopFeatureExtractor> {
  static std::ofstream OutFile;
  static unsigned CodeIDCounter;

  static void initializeOutFile() {
    static bool initialized = false;
    if (!initialized) {
      errs() << "Initializing loop_features.csv\n";
      OutFile.open("loop_features.csv", std::ios::out | std::ios::app);
      if (!OutFile.is_open()) {
        errs() << "Error: Could not open loop_features.csv\n";
        return;
      }
      std::ifstream checkFile("loop_features.csv");
      bool writeHeader = checkFile.peek() == std::ifstream::traits_type::eof();
      checkFile.close();
      if (writeHeader) {
        OutFile << "CodeID,Function,LoopHeader,num_instr,num_phis,num_calls,num_preds,num_succ,"
                << "ends_with_unreachable,ends_with_return,ends_with_cond_branch,"
                << "ends_with_branch,num_float_ops,nums_branchs,num_operands,"
                << "num_memory_ops,num_unique_predicates,trip_count,num_uses,num_blocks_in_lp,loop_depth\n";
        OutFile.flush();
      }
      errs() << "loop_features.csv opened successfully\n";
      initialized = true;
    }
  }

  static void initializeCodeIDCounter() {
    std::ifstream idFile("code_id.txt");
    if (idFile.is_open()) {
      idFile >> CodeIDCounter;
      idFile.close();
      errs() << "Read CodeIDCounter: " << CodeIDCounter << " from code_id.txt\n";
    } else {
      CodeIDCounter = 0;
      errs() << "No code_id.txt found, initialized CodeIDCounter to 0\n";
    }
  }

  static void saveCodeIDCounter() {
    std::ofstream idFile("code_id.txt");
    if (idFile.is_open()) {
      idFile << CodeIDCounter;
      idFile.close();
      errs() << "Saved CodeIDCounter: " << CodeIDCounter << " to code_id.txt\n";
    } else {
      errs() << "Error: Could not save CodeIDCounter to code_id.txt\n";
    }
  }

  LoopFeatureExtractor() {
    errs() << "Constructing LoopFeatureExtractor\n";
    initializeOutFile();
    initializeCodeIDCounter();
  }

  ~LoopFeatureExtractor() {
    saveCodeIDCounter();
  }

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM) {
    static unsigned CurrentCodeID = CodeIDCounter++;
    errs() << "Running LoopFeatureExtractor on module with CodeID: " << CurrentCodeID << "\n";

    for (Function &F : M) {
      if (F.isDeclaration()) {
        errs() << "Skipping function " << F.getName() << " because it is a declaration\n";
        continue;
      }

      errs() << "Analyzing function: " << F.getName() << "\n";
      auto &FAM = MAM.getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
      auto &LI = FAM.getResult<LoopAnalysis>(F);
      auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

      size_t loopCount = 0;
      for (const Loop *L : LI) {
        ++loopCount;
      }
      errs() << "Number of loops detected in " << F.getName() << ": " << loopCount << "\n";
      if (loopCount == 0) {
        errs() << "No loops found in function: " << F.getName() << "\n";
      }

      for (Loop *L : LI) {
        errs() << "Analyzing loop with header: " << L->getHeader()->getName() << " in " << F.getName() << "\n";
        analyzeLoop(L, SE, F.getName(), CurrentCodeID);
      }
    }

    return PreservedAnalyses::all();
  }

  void analyzeLoop(Loop *L, ScalarEvolution &SE, StringRef FuncName, unsigned CurrentCodeID) {
    errs() << "Processing loop in " << FuncName << ", header: " << L->getHeader()->getName() << "\n";
    std::set<BasicBlock *> LoopBlocks(L->block_begin(), L->block_end());
    std::set<BasicBlock *> unique_preds;
    std::set<BasicBlock *> unique_succs;

    int num_instr = 0, num_phis = 0, num_calls = 0;
    int num_float_ops = 0, nums_branchs = 0, num_operands = 0, num_memory_ops = 0;
    int num_uses = 0, num_blocks_in_lp = 0, num_unique_predicates = 0;
    bool ends_with_unreachable = false, ends_with_return = false;
    bool ends_with_cond_branch = false, ends_with_branch = false;

    for (auto *BB : L->blocks()) {
      num_blocks_in_lp++;

      for (Instruction &I : *BB) {
        num_instr++;
        num_operands += I.getNumOperands();

        if (isa<PHINode>(I)) num_phis++;
        if (isa<CallBase>(I)) num_calls++;
        if (isa<LoadInst>(I) || isa<StoreInst>(I)) num_memory_ops++;
        if (isa<BranchInst>(I)) {
          nums_branchs++;
          if (cast<BranchInst>(&I)->isConditional()) ends_with_cond_branch = true;
          ends_with_branch = true;
        }
        if (I.getOpcode() == Instruction::FAdd || I.getOpcode() == Instruction::FSub ||
            I.getOpcode() == Instruction::FMul || I.getOpcode() == Instruction::FDiv) {
          num_float_ops++;
        }

        for (auto *U : I.users()) {
          if (Instruction *UserI = dyn_cast<Instruction>(U)) {
            if (L->contains(UserI->getParent())) num_uses++;
          }
        }
      }

      if (Instruction *TI = BB->getTerminator()) {
        if (isa<UnreachableInst>(TI)) ends_with_unreachable = true;
        if (isa<ReturnInst>(TI)) ends_with_return = true;
      }

      for (auto *Pred : predecessors(BB)) {
        unique_preds.insert(Pred);
      }
      for (auto *Succ : successors(BB)) {
        unique_succs.insert(Succ);
      }
    }

    num_unique_predicates = unique_preds.size();
    int num_preds = num_unique_predicates;
    int num_succ = unique_succs.size();

    int64_t trip_count = 0;
    if (auto *TC = SE.getBackedgeTakenCount(L)) {
      if (auto *ConstTC = dyn_cast<SCEVConstant>(TC)) {
        trip_count = ConstTC->getValue()->getZExtValue() + 1;
      } else {
        errs() << "Trip count not constant for loop in " << FuncName << "\n";
      }
    } else {
      errs() << "Could not compute trip count for loop in " << FuncName << "\n";
    }

    auto *Header = L->getHeader();
    OutFile << CurrentCodeID << ","
            << FuncName.str() << ","
            << Header->getName().str() << ","
            << num_instr << ","
            << num_phis << ","
            << num_calls << ","
            << num_preds << ","
            << num_succ << ","
            << (ends_with_unreachable ? 1 : 0) << ","
            << (ends_with_return ? 1 : 0) << ","
            << (ends_with_cond_branch ? 1 : 0) << ","
            << (ends_with_branch ? 1 : 0) << ","
            << num_float_ops << ","
            << nums_branchs << ","
            << num_operands << ","
            << num_memory_ops << ","
            << num_unique_predicates << ","
            << trip_count << ","
            << num_uses << ","
            << num_blocks_in_lp << ","
            << L->getLoopDepth()
            << "\n";
    OutFile.flush();

    errs() << "Wrote features for loop in " << FuncName << ", header: " << Header->getName() << ", CodeID: " << CurrentCodeID << "\n";

    for (Loop *SubLoop : L->getSubLoops()) {
      errs() << "Found subloop with header: " << SubLoop->getHeader()->getName() << " in " << FuncName << "\n";
      analyzeLoop(SubLoop, SE, FuncName, CurrentCodeID);
    }
  }
};

std::ofstream LoopFeatureExtractor::OutFile;
unsigned LoopFeatureExtractor::CodeIDCounter = 0;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "LoopFeatureExtractor", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, ModulePassManager &MPM, ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "loop-features") {
            MPM.addPass(LoopFeatureExtractor());
            return true;
          }
          return false;
        });
    }};
}
/*
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <memory>
#include <set>

using namespace llvm;

namespace {
struct LoopFeatureExtractor : public PassInfoMixin<LoopFeatureExtractor> {
  static std::ofstream OutFile;
  static unsigned CodeIDCounter;

  static void initializeOutFile() {
    static bool initialized = false;
    if (!initialized) {
      errs() << "Initializing loop_features.csv\n";
      OutFile.open("loop_features.csv", std::ios::out | std::ios::app);
      if (!OutFile.is_open()) {
        errs() << "Error: Could not open loop_features.csv\n";
        return;
      }
      std::ifstream checkFile("loop_features.csv");
      bool writeHeader = checkFile.peek() == std::ifstream::traits_type::eof();
      checkFile.close();
      if (writeHeader) {
        OutFile << "CodeID,Function,LoopHeader,num_instr,num_phis,num_calls,num_preds,num_succ,"
                << "ends_with_unreachable,ends_with_return,ends_with_cond_branch,"
                << "ends_with_branch,num_float_ops,nums_branchs,num_operands,"
                << "num_memory_ops,num_unique_predicates,trip_count,num_uses,num_blocks_in_lp,loop_depth\n";
        OutFile.flush();
      }
      errs() << "loop_features.csv opened successfully\n";
      initialized = true;
    }
  }

  static void initializeCodeIDCounter() {
    std::ifstream idFile("code_id.txt");
    if (idFile.is_open()) {
      idFile >> CodeIDCounter;
      idFile.close();
    } else {
      CodeIDCounter = 0;
    }
    errs() << "Initialized CodeIDCounter to " << CodeIDCounter << "\n";
  }

  static void saveCodeIDCounter() {
    std::ofstream idFile("code_id.txt");
    if (idFile.is_open()) {
      idFile << CodeIDCounter;
      idFile.close();
    } else {
      errs() << "Error: Could not save CodeIDCounter to code_id.txt\n";
    }
  }

  LoopFeatureExtractor() {
    errs() << "Constructing LoopFeatureExtractor\n";
    initializeOutFile();
    initializeCodeIDCounter();
  }

  ~LoopFeatureExtractor() {
    saveCodeIDCounter();
  }

  LoopFeatureExtractor(const LoopFeatureExtractor&) = delete;
  LoopFeatureExtractor& operator=(const LoopFeatureExtractor&) = delete;

  LoopFeatureExtractor(LoopFeatureExtractor&&) = default;
  LoopFeatureExtractor& operator=(LoopFeatureExtractor&&) = default;

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
    static unsigned CurrentCodeID = CodeIDCounter++;
    errs() << "Running LoopFeatureExtractor on function: " << F.getName() << " with CodeID: " << CurrentCodeID << "\n";
    if (F.getName() != "main") {
      errs() << "Skipping non-main function: " << F.getName() << "\n";
      return PreservedAnalyses::all();
    }
    if (F.isDeclaration()) {
      errs() << "Skipping function " << F.getName() << " because it is a declaration\n";
      return PreservedAnalyses::all();
    }

    auto &LI = FAM.getResult<LoopAnalysis>(F);
    auto &SE = FAM.getResult<ScalarEvolutionAnalysis>(F);

    size_t loopCount = 0;
    for (const Loop *L : LI) {
      ++loopCount;
    }
    errs() << "Number of loops detected in " << F.getName() << ": " << loopCount << "\n";
    if (loopCount == 0) {
      errs() << "No loops found in function: " << F.getName() << "\n";
    }

    for (Loop *L : LI) {
      errs() << "Analyzing loop with header: " << L->getHeader()->getName() << " in " << F.getName() << "\n";
      analyzeLoop(L, SE, F.getName(), CurrentCodeID);
    }

    return PreservedAnalyses::all();
  }

  void analyzeLoop(Loop *L, ScalarEvolution &SE, StringRef FuncName, unsigned CurrentCodeID) {
    errs() << "Processing loop in " << FuncName << ", header: " << L->getHeader()->getName() << "\n";
    std::set<BasicBlock *> LoopBlocks(L->block_begin(), L->block_end());
    std::set<BasicBlock *> unique_preds;
    std::set<BasicBlock *> unique_succs;

    int num_instr = 0, num_phis = 0, num_calls = 0;
    int num_float_ops = 0, nums_branchs = 0, num_operands = 0, num_memory_ops = 0;
    int num_uses = 0, num_blocks_in_lp = 0, num_unique_predicates = 0;
    bool ends_with_unreachable = false, ends_with_return = false;
    bool ends_with_cond_branch = false, ends_with_branch = false;

    for (auto *BB : L->blocks()) {
      num_blocks_in_lp++;

      for (Instruction &I : *BB) {
        num_instr++;
        num_operands += I.getNumOperands();

        if (isa<PHINode>(I)) num_phis++;
        if (isa<CallBase>(I)) num_calls++;
        if (isa<LoadInst>(I) || isa<StoreInst>(I)) num_memory_ops++;
        if (isa<BranchInst>(I)) {
          nums_branchs++;
          if (cast<BranchInst>(&I)->isConditional()) ends_with_cond_branch = true;
          ends_with_branch = true;
        }
        if (I.getOpcode() == Instruction::FAdd || I.getOpcode() == Instruction::FSub ||
            I.getOpcode() == Instruction::FMul || I.getOpcode() == Instruction::FDiv) {
          num_float_ops++;
        }

        for (auto *U : I.users()) {
          if (Instruction *UserI = dyn_cast<Instruction>(U)) {
            if (L->contains(UserI->getParent())) num_uses++;
          }
        }
      }

      if (Instruction *TI = BB->getTerminator()) {
        if (isa<UnreachableInst>(TI)) ends_with_unreachable = true;
        if (isa<ReturnInst>(TI)) ends_with_return = true;
      }

      for (auto *Pred : predecessors(BB)) {
        unique_preds.insert(Pred);
      }
      for (auto *Succ : successors(BB)) {
        unique_succs.insert(Succ);
      }
    }

    num_unique_predicates = unique_preds.size();
    int num_preds = num_unique_predicates;
    int num_succ = unique_succs.size();

    int64_t trip_count = 0;
    if (auto *TC = SE.getBackedgeTakenCount(L)) {
      if (auto *ConstTC = dyn_cast<SCEVConstant>(TC)) {
        trip_count = ConstTC->getValue()->getZExtValue() + 1;
      } else {
        errs() << "Trip count not constant for loop in " << FuncName << "\n";
      }
    } else {
      errs() << "Could not compute trip count for loop in " << FuncName << "\n";
    }

    auto *Header = L->getHeader();
    OutFile << CurrentCodeID << ","
            << FuncName.str() << ","
            << Header->getName().str() << ","
            << num_instr << ","
            << num_phis << ","
            << num_calls << ","
            << num_preds << ","
            << num_succ << ","
            << (ends_with_unreachable ? 1 : 0) << ","
            << (ends_with_return ? 1 : 0) << ","
            << (ends_with_cond_branch ? 1 : 0) << ","
            << (ends_with_branch ? 1 : 0) << ","
            << num_float_ops << ","
            << nums_branchs << ","
            << num_operands << ","
            << num_memory_ops << ","
            << num_unique_predicates << ","
            << trip_count << ","
            << num_uses << ","
            << num_blocks_in_lp << ","
            << L->getLoopDepth()
            << "\n";
    OutFile.flush();

    errs() << "Wrote features for loop in " << FuncName << ", header: " << Header->getName() << ", CodeID: " << CurrentCodeID << "\n";

    for (Loop *SubLoop : L->getSubLoops()) {
      errs() << "Found subloop with header: " << SubLoop->getHeader()->getName() << " in " << FuncName << "\n";
      analyzeLoop(SubLoop, SE, FuncName, CurrentCodeID);
    }
  }
};

std::ofstream LoopFeatureExtractor::OutFile;
unsigned LoopFeatureExtractor::CodeIDCounter = 0;
}

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "LoopFeatureExtractor", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM, ArrayRef<PassBuilder::PipelineElement>) {
          if (Name == "loop-features") {
            FPM.addPass(LoopFeatureExtractor());
            return true;
          }
          return false;
        });
    }};
} */
