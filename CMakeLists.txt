cmake_minimum_required(VERSION 3.13)
message("Current CMake Version: ${CMAKE_VERSION}")
project(SVFTypeInferPass LANGUAGES C CXX)

# cmake include
include(FetchContent) 

# ---- C++ config ----
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -Wall -fno-rtti")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-macro-redefined -Wno-error=macro-redefined")

# ---- LLVM config ----
find_package(LLVM 14 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# LLVM 14 helpers
list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# LLVM's include and compile defs
include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# add svf dependency
FetchContent_Declare(
  svf
  GIT_REPOSITORY "https://github.com/SVF-tools/SVF.git"
  GIT_TAG        SVF-2.9
	GIT_SUBMODULES_RECURSE TRUE
)

# declare svf and add includes
FetchContent_MakeAvailable(svf)
FetchContent_GetProperties(svf)

include_directories(${svf_SOURCE_DIR}/include)
include_directories(${svf_BINARY_DIR}/include)

# add library folder 
add_subdirectory(src)
