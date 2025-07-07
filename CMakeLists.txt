cmake_minimum_required(VERSION 3.20)
project(LoopFeatureExtractor)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})
add_llvm_library(LoopFeatureExtractorPlugin MODULE
  LoopFeatureExtractor.cpp
  DEPENDS
  intrinsics_gen
  PLUGIN_TOOL
  opt
)
set_target_properties(LoopFeatureExtractorPlugin PROPERTIES
  COMPILE_FLAGS "-fno-rtti"
)
