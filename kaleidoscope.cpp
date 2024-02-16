#include <string>
#include <memory>
#include <utility>
#include <vector>
#include <cctype>
#include <cassert>
#include <string>
#include <map>

#include "kaleidoscopeJIT.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/StandardInstrumentations.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"
#include "llvm/Transforms/Scalar/Reassociate.h"
#include "llvm/Transforms/Scalar/SimplifyCFG.h"

using namespace llvm;
using namespace llvm::orc;

/*
    =========================
    ========= LEXER =========
    ========================= 
*/

// The lexer returns tokens [0-255] if its an unknown charachter, otherwise one of the known tokens
enum Token {
    TOK_EOF = -1,
    
    // commands
    TOK_DEF = -2,
    TOK_EXTERN = -3,
    
    // primary
    TOK_IDENTIFIER = -4, // identifier can be variable name, function name
    TOK_NUMBER = -5,

    // contorl flow (if statements)
    TOK_IF = -6,
    TOK_THEN = -7,
    TOK_ELSE = -8,

    // control flow (for loops)
    TOK_FOR = -9,
    TOK_IN = -10
};

/*
    Each token returned by our lexer will either be one of the Token enum values 
    or it will be an ‘unknown’ character like ‘+’, which is returned as its ASCII value. 

    If the current token is an identifier, the IdentifierStr global variable holds the name of the identifier. 
    If the current token is a numeric literal (like 1.0), NumVal holds its value.
*/

static std::string IdentifierStr; // Filled in if TOK_IDENTIFIER
static double NumVal;             // Filled in if TOK_NUMBER


int gettok() {
    static int lastChar = ' ';
    
    // skip whitespaces
    while (isspace(lastChar)) {
        lastChar = getchar();
    }
    
    // [a-zA-Z][a-zA-Z0-9]*
    if (isalpha(lastChar)) {
        IdentifierStr = lastChar;

        while (isalnum(lastChar = getchar()))
            IdentifierStr += lastChar;
        
        if (IdentifierStr == "def")
            return TOK_DEF;
        if (IdentifierStr == "extern")
            return TOK_EXTERN;
        if (IdentifierStr == "if")
            return TOK_IF;
        if (IdentifierStr == "then")
            return TOK_THEN;
        if (IdentifierStr == "else")
            return TOK_ELSE;
        if (IdentifierStr == "for")
            return TOK_FOR;
        if (IdentifierStr == "in")
            return TOK_IN;
         

        return TOK_IDENTIFIER;
    }
    
    // Number: [0-9.]+
    if (isdigit(lastChar) || lastChar == '.') {
        std::string numStr = "";
        
        do {
            numStr += lastChar;
            lastChar = getchar();
        } while (isdigit(lastChar) || lastChar == '.');

        NumVal = strtod(numStr.c_str(), nullptr); // string to numeric value
        return TOK_NUMBER;
    }
    
    // ignore comments
    if (lastChar == '#') {
        do 
            lastChar = getchar();
        while (lastChar != EOF && lastChar != '\n' && lastChar != '\r');

        if (lastChar != EOF)
            return gettok();
    }
    
    // check for end of file
    if (lastChar == EOF)
        return TOK_EOF;
   
    // if it doesnt match any of the above we return the default ascii value
    int thisChar = lastChar;
    lastChar = getchar();
    return thisChar;
}

/*
    ==============================================
    ========= AST (Abstract Syntax Tree) ========= 
    ==============================================
*/

/*
    The AST for a program captures its behavior in such a way that it is easy for later stages 
    of the compiler (e.g. code generation) to interpret. 

    We basically want one object for each construct in the language. In Kaleidoscope, 
    we have expressions, a prototype, and a function object.
*/

// base class for all expression nodes in the AST 
// Note: This is an abstract class
namespace {
    class ExprAST {
    public:
        virtual ~ExprAST() = default;
        
        /*
            The codegen() method says to emit IR for that AST node along with all the 
            things it depends on, and they all return an LLVM Value object. 
            
            “Value” is the class used to represent a 
            “Static Single Assignment (SSA) register” 
            or “SSA value” in LLVM. 
        */
        virtual Value* codegen() = 0;
    };

    // number node
    class NumberExprAST : public ExprAST {
        double Val;
        
    public:
        NumberExprAST(double Val) : Val(Val) {}
        Value* codegen() override;
    };

    // variable/identifier node
    class VariableExprAST : public ExprAST {
        std::string Name;

    public:
        VariableExprAST(const std::string &Name) : Name(Name) {}
        Value* codegen() override;
    };

    // binary expression node
    class BinaryExprAST : public ExprAST {
        char Op; // operator of binary expr (e.g. '+', '-', '*', '/')
        std::unique_ptr<ExprAST> LHS, RHS; // left & right hand sides of the expression (e.g. operands)

    public:
        BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS) 
            : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
        Value* codegen() override;
    };

    class CallExprAST : public ExprAST {
        std::string Callee;
        std::vector<std::unique_ptr<ExprAST>> Args;

    public:
        CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args) 
            : Callee(Callee), Args(std::move(Args)) {}
        Value* codegen() override;
    };

    class IfExprAST : public ExprAST {
        std::unique_ptr<ExprAST> Cond, Then, Else;
    public:
        IfExprAST(std::unique_ptr<ExprAST> Cond, std::unique_ptr<ExprAST> Then,std::unique_ptr<ExprAST> Else) :
            Cond(std::move(Cond)), Then(std::move(Then)), Else(std::move(Else)) {}

        Value* codegen() override;
    };

    /* 
        PrototypeAST - This class represents the "prototype" for a function,
        which captures its name, and its argument names (thus implicitly the number
        of arguments the function takes).
    */

    class ForExprAST : public ExprAST {
        std::string VarName;
        std::unique_ptr<ExprAST> Start, End, Step, Body;
    public:
        ForExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Start,
            std::unique_ptr<ExprAST> End, std::unique_ptr<ExprAST> Step, 
            std::unique_ptr<ExprAST> Body) : VarName(VarName), Start(std::move(Start)), 
            End(std::move(End)), Step(std::move(Step)), Body(std::move(Body)) {}
    
        Value* codegen() override;
    };

    class PrototypeAST {
        std::string Name;
        std::vector<std::string> Args;

    public:
        PrototypeAST(const std::string &Name, std::vector<std::string> Args) 
            : Name(Name), Args(std::move(Args)) {}
        
        const std::string &getName() const { return Name; }
        Function* codegen();
    };

    // FunctionAST - This class represents a function definition itself.
    class FunctionAST {
        std::unique_ptr<PrototypeAST> Proto;
        std::unique_ptr<ExprAST> Body;

    public:
        FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
            : Proto(std::move(Proto)), Body(std::move(Body)) {}
        Function* codegen();
    };
}

/*
    ==========================
    ========= PARSER =========
    ========================== 
*/
static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

// BinopPrecedence - This holds the precedence for each binary operator that is defined
static std::map<char, int> BinopPrecedence = {
    {'<', 10},
    {'>', 10},
    {'+', 20},
    {'-', 20},
    {'*', 40},
    {'/', 40}
};

// GetTokPrecedence - Get the precedence of the pending binary operator token.
static int GetTokPrecedence() {
    if (!isascii(CurTok))
        return -1;

    // Make sure it's a declared binop.
    int TokPrec = BinopPrecedence[CurTok];
    if (TokPrec <= 0)
        return -1;
    return TokPrec;
}

// LogError* - These are little helper functions for error handling
std::unique_ptr<ExprAST> LogError(const char *Str) {
    fprintf(stderr, "Error: %s\n", Str);
    return nullptr;
}
std::unique_ptr<PrototypeAST> LogErrorP(const char *Str) {
    LogError(Str);
    return nullptr;
}

static std::unique_ptr<ExprAST> ParseExpression();
static std::unique_ptr<ExprAST> ParseIfExpr();
static std::unique_ptr<ExprAST> ParseForExpr();
/*

    Production Rule:
    NumberExpr -> number literal
*/
static std::unique_ptr<ExprAST> ParseNumberExpr() {
    auto Result = std::make_unique<NumberExprAST>(NumVal);
    getNextToken(); // consume the number
    return std::move(Result);
}

/*
    Production Rule:
    ParenExpr -> '(' expression ')'
*/
static std::unique_ptr<ExprAST> ParseParenExpr() {
    getNextToken(); // eat (.
    auto V = ParseExpression();
    if (!V)
        return nullptr;

    if (CurTok != ')')
        return LogError("expected ')'");
    getNextToken(); // eat ).
    return V;
}

/*
    Production Rule:
    Identifier -> '(' expression* ')'
*/
static std::unique_ptr<ExprAST> ParseIdentifierExpr() {
    std::string IdName = IdentifierStr;

    getNextToken(); // eat identifier.

    if (CurTok != '(') // Simple variable ref.
        return std::make_unique<VariableExprAST>(IdName);

    // Call.
    getNextToken(); // eat (
    std::vector<std::unique_ptr<ExprAST>> Args;
    if (CurTok != ')') {
        while (true) {
            if (auto Arg = ParseExpression())
                Args.push_back(std::move(Arg));
            else
                return nullptr;

            if (CurTok == ')')
                break;

            if (CurTok != ',')
                return LogError("Expected ')' or ',' in argument list");
            getNextToken();
        }
    }
    // Eat the ')'.
    getNextToken();

    return std::make_unique<CallExprAST>(IdName, std::move(Args));
}

/*
    Production Rule:
    Primary -> IdentifierExpr | NumberExpr | ParenExpr
*/
static std::unique_ptr<ExprAST> ParsePrimary() {
    switch (CurTok) {
    default:
        return LogError("unknown token when expecting an expression");
    case TOK_IDENTIFIER:
        return ParseIdentifierExpr();
    case TOK_NUMBER:
        return ParseNumberExpr();
    case TOK_IF:
        return ParseIfExpr();
    case TOK_FOR:
        return ParseForExpr();
    case '(':
        return ParseParenExpr();
    }
}

/*
    Production Rule:
    BinOpRHS -> ('+' primary)*
*/
static std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
    // If this is a binop, find its precedence.
    while (true) {
        int TokPrec = GetTokPrecedence();

    // If this is a binop that binds at least as tightly as the current binop,
    // consume it, otherwise we are done.
    if (TokPrec < ExprPrec) return LHS;

    // Okay, we know this is a binop.
    int BinOp = CurTok;
    getNextToken(); // eat binop

    // Parse the primary expression after the binary operator.
    auto RHS = ParsePrimary();
    if (!RHS) return nullptr;

    // If BinOp binds less tightly with RHS than the operator after RHS, let
    // the pending operator take RHS as its LHS.
    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS) return nullptr;
    }

    // Merge LHS/RHS.
    LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

/*
    Production Rule:
    Expression -> primary binoprhs
*/
static std::unique_ptr<ExprAST> ParseExpression() {
    auto LHS = ParsePrimary();
    if (!LHS) return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

/*
    Production Rule:
    Prototype ->  id '(' id* ')'
*/
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    if (CurTok != TOK_IDENTIFIER)
        return LogErrorP("Expected function name in prototype");

    std::string FnName = IdentifierStr;
    getNextToken();

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype");

    std::vector<std::string> ArgNames;
    while (getNextToken() == TOK_IDENTIFIER)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype");

    getNextToken(); // eat ')'.

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames));
}

/*
    Production Rule:
    definition -> 'def' prototype expression

    Note: 'def' is a keyword
*/
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat def.
    auto Proto = ParsePrototype();
    if (!Proto)
        return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Make an anonymous proto
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

/*
    Production Rule:
    external -> 'extern' prototype

    Note: 'extern' is a keyword
*/
static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat extern.
    return ParsePrototype();
}

/*
    Production Rule:

    IfExpression -> 'if' expression 'then' expression 'else' expression
*/
static std::unique_ptr<ExprAST> ParseIfExpr() {
    getNextToken(); // eat if

    auto Cond = ParseExpression(); // parse expression
    if (!Cond) 
        return nullptr;

    if (CurTok != TOK_THEN) 
        return LogError("Exprected then.");
    getNextToken(); // eat then

    auto Then = ParseExpression();
    if (!Then) 
        return nullptr;

    if (CurTok != TOK_ELSE)
        return LogError("expected else");

    getNextToken();

    auto Else = ParseExpression();
    if (!Else)
        return nullptr;

    return std::make_unique<IfExprAST>(std::move(Cond), std::move(Then), std::move(Else));
}

/*
    Production Rule:
    ForExpr -> 'for' identifier '=' expr ',' (',' expr)? 'in' expression
*/
static std::unique_ptr<ExprAST> ParseForExpr() {
    getNextToken();
    
    if (CurTok != TOK_IDENTIFIER)
        return LogError("expected identifier after for\n");
    std::string idName = IdentifierStr;
    getNextToken();

    if (CurTok != '=')
        return LogError("Expected '=' after for\n");
    getNextToken();
    
    auto Start = ParseExpression();
    if (!Start) return nullptr;
    if (CurTok != ',')
        return LogError("expected ',' after for start value\n");
    getNextToken();

    auto End = ParseExpression();
    if (!End) return nullptr;

    // Step value is optional
    std::unique_ptr<ExprAST> Step;
    if (CurTok == ',') {
        getNextToken();
        Step = ParseExpression();
        if (!Step) return nullptr;
    }

    if (CurTok != TOK_IN)
        return LogError("expected 'in' after for.");
    getNextToken();

    auto Body = ParseExpression();
    if (!Body) return nullptr;
    return std::make_unique<ForExprAST>(idName, std::move(Start), std::move(End), std::move(Step), std::move(Body));
}

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

/*
    'LogErrorV' Method will be used to report errors found 
    during code generation (for example, use of an undeclared parameter):
*/
Value* LogErrorV(const char* Str) {
    LogError(Str);
    return nullptr;
}

Function* getFunction(std::string Name) {
    // First, see if the function has already been added to the current module
    if (auto* F = TheModule->getFunction(Name))
        return F;
    
    // If not check if we can codegen the decleration from some existing prototype
    auto FI = FunctionProtos.find(Name);
    if (FI != FunctionProtos.end())
        return FI->second->codegen();

    // If no existing prototype exist, return null
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
            return LogErrorV("Invalid binary operator.");
    }
}

Value* CallExprAST::codegen() {
    // We lookup the function name in the global module table
    Function* CalleeFunction = getFunction(Callee);

    if (!CalleeFunction) 
        return LogErrorV("Unknown function referenced.");

    int e = Args.size(); // number of args
    if (CalleeFunction->arg_size() != e) 
        return LogErrorV("Incorrect number of arguments passed.");

    std::vector<Value*> ArgsV; // will store the LLVM IR for the function arguments

    for (unsigned i = 0; i != e; ++i) {
        ArgsV.push_back(Args[i]->codegen());

        if (!ArgsV.back()) return nullptr;
    }

    return Builder->CreateCall(CalleeFunction, ArgsV, "calltmp");
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

    Function* F = Function::Create(FT, Function::ExternalLinkage, Name, TheModule.get()); // creates the IR Function 
                                                                                          // corresponding to the Prototype.

    // ExternalLinkage in the above line means that the function may be defined 
    // and/or that its callable by functions outside the module outside the current module

    /*
        Next three lines sets names for all arguments according to the names given in the Prototype.
        -- This is optional but it makes the IR more readable --
    */
    unsigned idx = 0;
    for (auto &Arg : F->args())
        Arg.setName(Args[idx++]);

    return F; // Return function IR
}

/*
    At this point we have a function prototype with no body. 
*/
Function* FunctionAST::codegen() {

    // Transfer ownership of the prototype to the FunctionProtos map, but keep a
    // reference to it for use below.
    auto& P = *Proto;
    FunctionProtos[Proto->getName()] = std::move(Proto);
    Function *TheFunction = getFunction(P.getName());
    if (!TheFunction)
        return nullptr;

    // Create new basic block to start insertion into
    BasicBlock* BB = BasicBlock::Create(*TheContext, "entry", TheFunction);
    Builder->SetInsertPoint(BB);

    // Record the function arguments in NamedValues map (e.g. The Symbol Table)
    NamedValues.clear();
    for (auto &Arg : TheFunction->args()) 
        NamedValues[std::string(Arg.getName())] = &Arg;

    if (Value* RetVal = Body->codegen()) { // Generate IR for the body
        // Finish of the function 

        Builder->CreateRet(RetVal); // Creates 'ret' instruction for the function (Used to return control flow)
        verifyFunction(*TheFunction); // Does consistency checks on the generated code and validates it

        // Optimize the function 
        TheFPM->run(*TheFunction, *TheFAM);

        return TheFunction;
    }

    /*
        Its important to erase the fucntion if we found errors to allow the 
        user to redefine a function that they incorrectly typed in before. 
        
        Ff we didn’t delete it, it would live in the symbol table, 
        with a body, preventing future redefinition.
    */
    TheFunction->eraseFromParent();
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

/*
    ================================================
    ========= TOP-lEVEL PARSING JIT DRIVER ========= 
    ================================================
*/
static void InitializeModuleAndPassManager() {
    // Open a new context module
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("KaleidoscopeJIT", *TheContext);
    TheModule->setDataLayout(TheJIT->getDataLayout());    

    // Create new builder for the module
    Builder = std::make_unique<IRBuilder<>>(*TheContext);

    // Create new pass analysis managers
    TheFPM = std::make_unique<FunctionPassManager>();
    TheLAM = std::make_unique<LoopAnalysisManager>();
    TheFAM = std::make_unique<FunctionAnalysisManager>();

    TheCGAM = std::make_unique<CGSCCAnalysisManager>();
    TheMAM = std::make_unique<ModuleAnalysisManager>();
    ThePIC = std::make_unique<PassInstrumentationCallbacks>();
    TheSI = std::make_unique<StandardInstrumentations>(*TheContext, /*DebugLogging*/ true);
    TheSI->registerCallbacks(*ThePIC, TheMAM.get());

/*
    Once these managers are set up, we use a series of 
    “addPass” calls to add a bunch of LLVM transform passes
*/

    // Do simple "peephole" optimizations and bit-twiddling optimizations.
    TheFPM->addPass(InstCombinePass());
    // Reassociate expressions.
    TheFPM->addPass(ReassociatePass());
    // Eliminate Common SubExpressions.
    TheFPM->addPass(GVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    TheFPM->addPass(SimplifyCFGPass());

/*
    Next, we register the analysis passes 
    used by the transform passes.
*/
    PassBuilder PB;
    PB.registerModuleAnalyses(*TheMAM);
    PB.registerFunctionAnalyses(*TheFAM);
    PB.crossRegisterProxies(*TheLAM, *TheFAM, *TheCGAM, *TheMAM);
}

static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto *FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read function definition:");
            FnIR->print(errs());
            fprintf(stderr, "\n");
            ExitOnErr(TheJIT->addModule(ThreadSafeModule(std::move(TheModule), std::move(TheContext))));
            InitializeModuleAndPassManager();
        }
    } else {
        // Skip token for error recovery
        getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto *FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Read extern: ");
            FnIR->print(errs());
            fprintf(stderr, "\n");
            FunctionProtos[ProtoAST->getName()] = std::move(ProtoAST);
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function
    if (auto FnAST = ParseTopLevelExpr()) {
        if (FnAST->codegen()) {
            // Create a ResourceTracker to track JIT'd memory allocated to our
            // anonymous expression -- that way we can free it after executing
            auto RT = TheJIT->getMainJITDylib().createResourceTracker();

            auto TSM = ThreadSafeModule(std::move(TheModule), std::move(TheContext));
            ExitOnErr(TheJIT->addModule(std::move(TSM), RT));
            InitializeModuleAndPassManager();

            // Search the JIT for the __anon_expr symbol
            auto ExprSymbol = ExitOnErr(TheJIT->lookup("__anon_expr"));

            // Get the symbol's address and cast it to the right type (takes no
            // arguments, returns a double) so we can call it as a native function
            double (*FP)() = ExprSymbol.getAddress().toPtr<double (*)()>();
            fprintf(stderr, "Evaluated to %f\n", FP());

            // Delete the anonymous expression module from the JIT.
            ExitOnErr(RT->remove());
        }
    } else {
        getNextToken(); // Skip token for error recovery
    }
}

// top ::= definition | external | expression | ';'
static void MainLoop() {
    while (true) {
        fprintf(stderr, "ready> ");
        switch (CurTok) {
            case TOK_EOF:
            return;
        case ';': // ignore top-level semicolons.
            getNextToken();
            break;
        case TOK_DEF:
            HandleDefinition();
            break;
        case TOK_EXTERN:
            HandleExtern();
            break;
        default:
            HandleTopLevelExpression();
        break;
    }
  }
}

int main() {
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();
    InitializeNativeTargetAsmParser();

    // Prime the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    TheJIT = ExitOnErr(KaleidoscopeJIT::Create());
    
    // Make the module which holds all the code
    InitializeModuleAndPassManager();

    // Run the main "interpreter loop" now.
    MainLoop();
    return 0;
}