#ifndef __CODEGEN_H__
#define __CODEGEN_H__

#include "common.h"
#include "AST.h"
#include "Parser.h"

/*
    ===================================
    ========= CODE GENERATION ========= 
    ===================================
*/

static std::unique_ptr<LLVMContext> TheContext;
static std::unique_ptr<Module> TheModule;
static std::unique_ptr<IRBuilder<>> Builder;
static std::map<std::string, Value*> NamedValues;


static std::unique_ptr<KaleidoscopeJIT> TheJIT; // Pointer to a simple JIT compiler
static std::unique_ptr<FunctionPassManager> TheFPM; // pointer to a Function Pass Manager (FPM), which manages
                                                    // a series of passes to optimize functions in LLVM IR

// pointers to unique instances of various analysis managers
static std::unique_ptr<LoopAnalysisManager> TheLAM;
static std::unique_ptr<FunctionAnalysisManager> TheFAM;
static std::unique_ptr<CGSCCAnalysisManager> TheCGAM;
static std::unique_ptr<ModuleAnalysisManager> TheMAM;


static std::unique_ptr<PassInstrumentationCallbacks> ThePIC;
static std::unique_ptr<StandardInstrumentations> TheSI;
static std::map<std::string, std::unique_ptr<PrototypeAST>> FunctionProtos;
static ExitOnError ExitOnErr;

#endif