#include <string>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>
#include <cctype>
#include <string>
#include <map>

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
};

// number node
class NumberExprAST : public ExprAST {
    double Val;
    
public:
    NumberExprAST(double Val) : Val(Val) {}
};

// variable/identifier node
class VariableExprAST : public ExprAST {
    std::string Name;

public:
    VariableExprAST(const std::string &Name) : Name(Name) {}
};

// binary expression node
class BinaryExprAST : public ExprAST {
    char Op; // operator of binary expr (e.g. '+', '-', '*', '/')
    std::unique_ptr<ExprAST> LHS, RHS; // left & right hand sides of the expression (e.g. operands)

public:
    BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS) 
        : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
};

class CallExprAST : public ExprAST {
    std::string Callee;
    std::vector<std::unique_ptr<ExprAST>> Args;

public:
    CallExprAST(const std::string &Callee, std::vector<std::unique_ptr<ExprAST>> Args) 
        : Callee(Callee), Args(std::move(Args)) {}
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
};

// FunctionAST - This class represents a function definition itself.
class FunctionAST {
    std::unique_ptr<PrototypeAST> Proto;
    std::unique_ptr<ExprAST> Body;

public:
    FunctionAST(std::unique_ptr<PrototypeAST> Proto, std::unique_ptr<ExprAST> Body)
        : Proto(std::move(Proto)), Body(std::move(Body)) {}
};

static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

// BinopPrecedence - This holds the precedence for each binary operator that is defined
static std::map<char, int> BinopPrecedence = {
    {'<', 10},
    {'+', 20},
    {'-', 20},
    {'*', 30}
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

/// definition ::= 'def' prototype expression
static std::unique_ptr<FunctionAST> ParseDefinition() {
    getNextToken(); // eat def.
    auto Proto = ParsePrototype();
    if (!Proto)
        return nullptr;

    if (auto E = ParseExpression())
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    return nullptr;
}

/// toplevelexpr ::= expression
static std::unique_ptr<FunctionAST> ParseTopLevelExpr() {
    if (auto E = ParseExpression()) {
        // Make an anonymous proto.
        auto Proto = std::make_unique<PrototypeAST>("__anon_expr", std::vector<std::string>());
        return std::make_unique<FunctionAST>(std::move(Proto), std::move(E));
    }
    return nullptr;
}

// external ::= 'extern' prototype
static std::unique_ptr<PrototypeAST> ParseExtern() {
    getNextToken(); // eat extern.
    return ParsePrototype();
}

static void HandleDefinition() {
    if (ParseDefinition()) {
        fprintf(stderr, "Parsed a function definition.\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleExtern() {
    if (ParseExtern()) {
        fprintf(stderr, "Parsed an extern\n");
    } else {
        // Skip token for error recovery.
        getNextToken();
    }
}

static void HandleTopLevelExpression() {
    // Evaluate a top-level expression into an anonymous function.
    if (ParseTopLevelExpr()) {
        fprintf(stderr, "Parsed a top-level expr\n");
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

  // Run the main "interpreter loop" now.
  MainLoop();

  return 0;
}