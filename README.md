# Loop_Unrolling_factor-prediction
# Loop Feature Extraction and Loop Unrolling Factor Prediction
# 📌Project Overview :
  This Project focuses on predicting the loop unrolling factor by extracting a set of loop features from various benchmarks .The extracted features serve as input for analyzing and training ML models that can predict optimal unrolling factors , thereby aiding compiler optimizations . 
# **TOOLS and Frameworks Used** 
- LLVM 17.0-for static analysis and writing custom passes .
- PolyBench - a benchmark suite 
- C++ - for implementating the LLVM plugin


## Directory structure 
.
├── loop-plugin/
│   ├── CMakeLists.txt
│   └── LoopFeatureExtractor.cpp
├── loop-pass-tests/
│   ├── polybench-ll/       # .ll files compiled from PolyBench
│   ├── loop_features.csv   # Output feature file
│   └── run_pass.sh         # Helper script to batch-run .ll files
├── polybench/              # PolyBench benchmark source
└──llvm-project             # LLVM 
|__README.md

# **STEPS**
### 1. LLVM setup (creates a llvm-project folder )
Downloaded and built the LLVM version 17 from source (github) . 
   ###### steps : 
    git clone https://github.com/llvm/llvm-project.git
    cd llvm-project folder
    mkdir build
    cd build
    cmake '-DLLVM_ENABLE_PROJECTS=clang;polly;openmp' -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles" ../llvm
    cmake --build .
    make install
    opt --version 
   
### 2.PolyBench setup (creates a polybench folder )
    -git clone https://github.com/rbaghdadi/polybench-3.2-3AC
Downloded the polyBench benchmark suite (github) and i stored as polybench. 


### 3.Developed LLVM pass (inside loop-plugin folder)
    Implemented a custom LLVM ModulePass named LoopFeatureExtractor.(a C++ code to extract features from loops )
    The Pass : 
       Analyzes each loop in every modules (main function and Other functions )
       Extarcts 18 features such as :   
          1. num_instr -> total no.of instructions 
          2. num_phis -> Number of PHI nodes in the loop header
          3. num_calls -> Number of function calls inside the loop
          4. num_preds -> Number of predecessor blocks
          5. num_succ -> Number of successor blocks
          6. ends_with_unreachable -> Boolean: Ends with unreachable instruction
          7. ends_with_return -> Boolean: Does the loop end with a return?
          8. ends_with_cond_branch -> Boolean: Ends with a conditional branch
          9. ends_with_branch -> Boolean: Ends with a branch
          10. num_float_ops -> Number of floating-point operations
          11. nums_branchs -> Number of branch instructions
          12. num_operands -> Number of operands used across instructions
          13. num_memory_ops -> Number of memory operations (load/store)
          14. num_unique_predicates -> Number of unique predecessor blocks
          15. trip_count -> Loop trip count if known statically, else estimated
          16. num_uses -> Number of uses of loop variables or operands
          17. num_blocks_in_lp -> Number of basic blocks in the loop
          18. loop_depth -> Nesting depth of the loop
        The extracted features are dumped into loop_features.csv file and it is stored in loop-pass-tests folder .
        And maintains a Unique ID for each input file .
 ### 4.CMake and Plugin Integration (inside loop-plugin folder )
  CMake configuration file (CMakeLists.txt) to build the plugin as a shared object .
 ###### For building : 
    cd ~/llvm/loop-plugin
    rm -rf build 
    mkdir build && cd build
    cmake .. -DLLVM_DIR=/home/llvm/llvm-project/build/lib/cmake/llvm
     make
    ~/llvm/llvm-project/build/bin/opt \
    -load ~/llvm/loop-plugin/build/libLLVMLoopFeatureExtractor.so \
    -loop-features \
    /home/llvm/loop-pass-tests/test1.ll \
    -disable-output


 ### 5.Create test cases : 
  Create a folder loop-pass-tests which stores all test cases and its .ll files .
  ###### For Creating .ll file for a test case
    <path_to_llvm-project>/build/bin/clang -emit-llvm -S -O0 -fno-unroll-loops -fno-vectorize -fno-inline -fno-slp-vectorize -fno-discard-value-names -Xclang -disable-llvm-passes -Xclang -disable-O0-optnone -o test-case.ll test-case.cpp
 ### 6.Running the Pass :
  Used custom opt in LLVM to apply the custom pass on .ll files generated from polyBench and other test cases .
   The result is a CSV file cointaining loop features .
  ###### For custom code : 
    <path_to_llvm-project>/build/bin/opt -load-pass-plugin=/home/cslab2/llvm/loop-plugin/build/LoopFeatureExtractorPlugin.so -passes='loop-simplify,loop-features' test-case.ll -o /dev/null 

  ###### For PolyBench : 
    <path_to_llvm-project>/build/bin/opt \
      -load-pass-plugin=<pth_to_loop-plugin>/build/LoopFeatureExtractorPlugin.so \
      -passes='loop-features' \
       polybench-ll/3mm.ll \
       -disable-output
  this is  for 3.mm module
 ### 7.Check loop_features.csv 




