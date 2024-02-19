cmake_minimum_required(VERSION 3.10)
project(kaleidoscope)

set(CLANG clang++)
find_package(LLVM REQUIRED CONFIG)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/headers)

file(GLOB SOURCES ${SRC_DIR}/*.cpp)

include_directories(${INCLUDE_DIR} ${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(kaleidoscope ${SOURCES})
target_link_libraries(kaleidoscope LLVMCore LLVMOrcJIT)

