#include "../headers/codegen.h"

/*
    'LogErrorV' Method will be used to report errors found 
    during code generation (for example, use of an undeclared parameter):
*/
Value* LogErrorV(const char* Str) {
    LogError(Str);
    return nullptr;
}

Function* getFunction(std::string Name) {
    // First, see if the function has already been added to the current module.
    if (auto *F = TheModule->getFunction(Name))
        return F;

    // If not, check whether we can codegen the declaration from some existing
    // prototype.
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end())
        return FI->second->codegen();

    // If no existing prototype exists, return null.
    return nullptr;
}

Value* NumberExprAST::codegen() {
    /*
        In the LLVM IR, numeric constants are represented with the ConstantFP class, 
        which holds the numeric value in an APFloat internally
    */
    return ConstantFP::get(*TheContext, APFloat(Val));
}

Value* VariableExprAST::codegen() {
    // Look this variable up in the symbol table
    Value* V = NamedValues[Name];
    if (!V) 
        LogErrorV("Unknown variable name.");
    return V;
}

/*
    -- BinaryExprAST::codegen --

    The basic idea here is that we recursively emit code for the left-hand side of the 
    expression then the right-hand side, then we compute the result of the binary expression
*/
Value* BinaryExprAST::codegen() {
    Value* L = LHS->codegen();
    Value* R = RHS->codegen();

    if (!R || !L) return nullptr;

    switch (Op) {
        case '+': return Builder->CreateFAdd(L, R, "addtmp"); // Builder's Floating point addition. 
        // The string "addtmp" is an optional string argument that provides a name for the resulting LLVM IR
        case '-': return Builder->CreateFSub(L, R, "subtmp"); // Builder's Floating point subtraction
        case '*': return Builder->CreateFMul(L, R, "multmp");
        case '/': return Builder->CreateFDiv(L, R, "divtmp");
        case '<': 
            L = Builder->CreateFCmpULT(L, R, "cmptmp"); // ULT (Unordered or Less Than)
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
        case '>':
            L = Builder->CreateFCmpOGT(L, R, "cmptmp"); // UGT (Unordered or Greater Than)
            return Builder->CreateUIToFP(L, Type::getDoubleTy(*TheContext), "booltmp");
        default: 
            break;
    }

    // IF it wasn't a builtin binary operator, it must be a user defined one
    Function* F = getFunction(std::string("binary") + Op);
    assert(F && "binary operator not found!\n");

    Value* Ops[] = {L, R};
    return Builder->CreateCall(F, Ops, "binop");
}

Value* UnaryExprAST::codegen() {
    Value* OperandV = Operand->codegen();
    if (!OperandV)
        return nullptr;

    Function* F = getFunction(std::string("unary") + Opcode);
    if (!F)
        return LogErrorV("Unknown unary operator");

    return Builder->CreateCall(F, OperandV, "unop");
}

Value* CallExprAST::codegen() {
    // Look up the name in the global module table.
    Function* CalleeF = getFunction(Callee);
    if (!CalleeF)
        return LogErrorV("Unknown function referenced");

    // If argument mismatch error.
    if (CalleeF->arg_size() != Args.size())
        return LogErrorV("Incorrect # arguments passed");

    std::vector<Value*> ArgsV;
    for (unsigned i = 0, e = Args.size(); i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());
        if (!ArgsV.back())
            return nullptr;
    }
    return Builder->CreateCall(CalleeF, ArgsV, "calltmp");
}

Value* IfExprAST::codegen() {
    Value* CondV = Cond->codegen();
    if (!CondV) 
        return nullptr;

    // Convert condition to a bool
    CondV = Builder->CreateFCmpONE(CondV, ConstantFP::get(*TheContext, APFloat(0.0)), "ifcond");

    // This code creates the basic blocks that are related to the if/then/else statement
    Function* TheFunction = Builder->GetInsertBlock()->getParent();

    BasicBlock* ThenBB = BasicBlock::Create(*TheContext, "then", TheFunction);
    BasicBlock* ElseBB = BasicBlock::Create(*TheContext, "else");
    BasicBlock* MergeBB = BasicBlock::Create(*TheContext, "ifcont");

    Builder->CreateCondBr(CondV, ThenBB, ElseBB); // adding conditional branch

    // Emit then value
    Builder->SetInsertPoint(ThenBB);

    Value* ThenV = Then->codegen();
    if (!ThenV)
        return nullptr;
    
    Builder->CreateBr(MergeBB);
    
    // Codegen of 'Then' can change the current block, update ThenBB for the PHI
    ThenBB = Builder->GetInsertBlock();

    // Emit else block.
    TheFunction->insert(TheFunction->end(), ElseBB);
    Builder->SetInsertPoint(ElseBB);

    Value* ElseV = Else->codegen();
    if (!ElseV)
        return nullptr;

    Builder->CreateBr(MergeBB);
    // codegen of 'Else' can change the current block, update ElseBB for the PHI
    ElseBB = Builder->GetInsertBlock();

    // Emit merge block
    TheFunction->insert(TheFunction->end(), MergeBB);
    Builder->SetInsertPoint(MergeBB);

    PHINode* PN = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, "iftmp");
    PN->addIncoming(ThenV, ThenBB);
    PN->addIncoming(ElseV, ElseBB);
    return PN;
}

/*
    -- PrototypeAST::codegen --

    This code packs a lot of power into a few lines. Note first that this function 
    returns a “Function*” instead of a “Value*”. Because a “prototype” really talks 
    about the external interface for a function (not the value computed by an expression)
*/
Function* PrototypeAST::codegen() {
    /*
        The call to FunctionType::get creates the FunctionType that should be used for a given Prototype. 
        
        Since all function arguments in Kaleidoscope are of type double, 
        the first line creates a vector of “N” LLVM double types. It then uses 
        the Functiontype::get method to create a function type that takes “N” 
        doubles as arguments.
    */
    std::vector<Type*> Doubles(Args.size(), Type::getDoubleTy(*TheContext));
    FunctionType* FT = FunctionType::get(Type::getDoubleTy(*TheContext), Doubles, false);

    Function* F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get());
                                                                               // corresponding to the Prototype.

    // ExternalLinkage in the above line means that the function may be defined 
    // and/or that its callable by functions outside the module outside the current module

    /*
        Next three lines sets names for all arguments according to the names given in the Prototype.
        -- This is optional but it makes the IR more readable --
    */
    unsigned Idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[Idx++]);
    return F;
}

/*
    At this point we have a function prototype with no body. 
*/
Function* FunctionAST::codegen() {
    // Transfer ownership of the prototype to the FunctionProtos map, but keep a
    // reference to it for use below.
    auto &P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    Function* TheFunction = getFunction(P.getName());
    if (!TheFunction)
        return nullptr;

    // If this is an operator, install it.
    if (P.isBinaryOp())
        BinopPrecedence[P.getOperatorName()] = P.getBinaryPrecedence();

    // Create a new basic block to start insertion into.
    BasicBlock* BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // Record the function arguments in the NamedValues map.
    NamedValues.clear();
    for (auto &Arg : TheFunction->args())
        NamedValues[std::string(Arg.getName())] = &Arg;

    if (Value* RetVal = Body->codegen()) {
        // Finish off the function.
        Builder->CreateRet(RetVal);

        // Validate the generated code, checking for consistency.
        verifyFunction(*TheFunction);

        // Run the optimizer on the function.
        TheFPM->run(*TheFunction, *TheFAM);

        return TheFunction;
    }

/*
    Its important to erase the fucntion if we found errors to allow the 
    user to redefine a function that they incorrectly typed in before. 
    
    Ff we didn’t delete it, it would live in the symbol table, 
    with a body, preventing future redefinition.
*/

    // Error reading body, remove function.
    TheFunction->eraseFromParent();

    if (P.isBinaryOp())
        BinopPrecedence.erase(P.getOperatorName());
    return nullptr;
}

Value* ForExprAST::codegen() {
    Value* StartVal = Start->codegen();
    if (!StartVal) return nullptr;

    // Make new basic block for the loop header, inserting after current block
    Function* TheFunction = Builder->GetInsertBlock()->getParent();
    BasicBlock* PreheaderBB = Builder->GetInsertBlock();
    BasicBlock* LoopBB = BasicBlock::Create(*TheContext, "loop", TheFunction);

    // Insert an explicit fall through from the current block to LoopBB
    Builder->CreateBr(LoopBB);

    Builder->SetInsertPoint(LoopBB);
    PHINode* Variable = Builder->CreatePHI(Type::getDoubleTy(*TheContext), 2, VarName);
    Variable->addIncoming(StartVal, PreheaderBB);

    Value* OldVal = NamedValues[VarName];
    NamedValues[VarName] = Variable;

    if (!Body->codegen()) return nullptr;

    Value* StepVal = nullptr;
    if (Step) {
        StepVal = Step->codegen();
        if (!StepVal) return nullptr;
    } else StepVal = ConstantFP::get(*TheContext, APFloat(1.0)); // default to 1.0

    Value* NextVar = Builder->CreateFAdd(Variable, StepVal, "nextvar");

    // Compute end condition    
    Value* EndCond = End->codegen();
    if (!EndCond) return nullptr;

    // Convert condition to a bool by comparing non-equal to 0.0
    EndCond = Builder->CreateFCmpONE(EndCond, ConstantFP::get(*TheContext, APFloat(0.0)), "loopcond");
    
    // Create the 'after loop' block and inset it
    BasicBlock* LoopEndBB = Builder->GetInsertBlock();
    BasicBlock* AfterBB = BasicBlock::Create(*TheContext, "afterloop", TheFunction);

    // Insert the conditional branch into the end of LoopEndBB
    Builder->CreateCondBr(EndCond, LoopBB, AfterBB);
    
    // Any new code will be inserted in AfterBB
    Builder->SetInsertPoint(AfterBB);

    // Add a new entry to the PHI node for the backedge
    Variable->addIncoming(NextVar, LoopEndBB);
    // restore the unshadowed variable
    if (OldVal) NamedValues[VarName] = OldVal;
    else NamedValues.erase(VarName);

    // for expr always returns 0.0
    return Constant::getNullValue(Type::getDoubleTy(*TheContext));
}

