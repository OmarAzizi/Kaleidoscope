#include <string>
#include <memory>
#include <utility>
#include <vector>
#include <cctype>
#include <string>
#include <map>

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"

using namespace llvm;

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
    TOK_NUMBER = -5
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

        return TOK_IDENTIFIER;
    }
    
    // Number: [0-9.]+
    if (isdigit(lastChar) || lastChar == '.') {
        std::string numStr;
        
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


/* 
    PrototypeAST - This class represents the "prototype" for a function,
    which captures its name, and its argument names (thus implicitly the number
    of arguments the function takes).
*/

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
    LHS =
        std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
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
    ===================================
    ========= CODE GENERATION ========= 
    ===================================
*/

static std::unique_ptr<LLVMContext> TheContext; 
static std::unique_ptr<IRBuilder<>> Builder; 
static std::unique_ptr<Module> TheModule; // This will own the memory of all the IR we generate
static std::map<std::string, Value*> NamedValues; // keeps track of which values are defined in the current
                                                  // scope and what their LLVM representation (e.g. It is a symbol table)

/*
    'LogErrorV' Method will be used to report errors found 
    during code generation (for example, use of an undeclared parameter):
*/
Value* LogErrorV(const char* Str) {
    LogError(Str);
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
    // We lookup the function name in the module table
    Function* CalleeFunction = TheModule->getFunction(Callee);

    if (!CalleeFunction) 
        return LogErrorV("Unknown function referenced.");

    int e = Args.size(); // number of args
    if (CalleeFunction->arg_size() != e) 
        return LogErrorV("Incorrect number of arguments passed.");

    std::vector<Value*> ArgsV; // will store the LLVM IR for the function arguments

    for (int i = 0; i < e; ++i) {
        ArgsV.push_back(Args[i]->codegen());

        if (!ArgsV.back()) return nullptr;
    }

    return Builder->CreateCall(CalleeFunction, ArgsV, "calltmp");
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
    FunctionType* FT = FunctionType::get(Type::getVoidTy(*TheContext), Doubles, false);

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
    // First check 'TheModule' for an existing function from previous 'extern' decleration
    Function* TheFunction = TheModule->getFunction(Proto->getName());

    if (!TheFunction) // Generate function Proto if it wasn't already generated
        TheFunction = Proto->codegen();

    if (!TheFunction) return nullptr;

    if (!TheFunction->empty())
        return (Function*)LogErrorV("Function cannot be redefined;");

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

/*
    ================================================
    ========= TOP-lEVEL PARSING JIT DRIVER ========= 
    ================================================
*/
static void InitializeModule() {
    // Open a new context module
    TheContext = std::make_unique<LLVMContext>();
    TheModule = std::make_unique<Module>("my cool jit", *TheContext);

    // Create new builder for the module
    Builder = std::make_unique<IRBuilder<>>(*TheContext);
}

static void HandleDefinition() {
    if (auto FnAST = ParseDefinition()) {
        if (auto* FnIR = FnAST->codegen()) { // Generating IR and printing it
            fprintf(stderr, "Read function definition:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern() {
    if (auto ProtoAST = ParseExtern()) {
        if (auto* FnIR = ProtoAST->codegen()) {
            fprintf(stderr, "Read extern:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function
    if (auto FnAST = ParseTopLevelExpr()) {
        if (auto* FnIR = FnAST->codegen()) {
            fprintf(stderr, "Read top-level expression:\n");
            FnIR->print(errs());
            fprintf(stderr, "\n");

            // Remove the anonymous expression
            FnIR->removeFromParent();
        }
    } else {
        // Skip token for error recovery.
        getNextToken();
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
    // Prime the first token.
    fprintf(stderr, "ready> ");
    getNextToken();

    // Make the module which holds all the code
    InitializeModule();

    // Run the main "interpreter loop" now.
    MainLoop();

    // Print all of the generated code
    TheModule->print(errs(), nullptr);
    return 0;
}