#include "../headers/Parser.h"

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
    case '(':
        return ParseParenExpr();
    case TOK_IF:
        return ParseIfExpr();
    case TOK_FOR:
        return ParseForExpr(); 
    }
}

/*
    Production Rule:

    Unary -> PrimaryExpr | '!' Unary
*/
static std::unique_ptr<ExprAST> ParseUnary() {
    // if the current token isnt an operator it must be a primay expr
    if (!isascii(CurTok) || CurTok == '(' || CurTok == ',')
        return ParsePrimary();

    // If this is a unary operator, read it
    int Opc = CurTok;
    getNextToken();
    if (auto Operand = ParseUnary())
        return std::make_unique<UnaryExprAST>(Opc, std::move(Operand));
    return nullptr;
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
    auto RHS = ParseUnary();
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
    auto LHS = ParseUnary();
    if (!LHS) return nullptr;
    return ParseBinOpRHS(0, std::move(LHS));
}

/*
    Production Rule:
    Prototype ->  id '(' id* ')' | binary LETTER number? (id, id)
*/
static std::unique_ptr<PrototypeAST> ParsePrototype() {
    std::string FnName;

    unsigned Kind = 0; // 0 = Identifier, 1 = Unary, 2 = Binary
    unsigned BinaryPrecedence = 30;

    switch (CurTok) {
        default:
            return LogErrorP("Expected function name in prototye\n");
        case TOK_IDENTIFIER:
            FnName = IdentifierStr;
            Kind = 0;
            getNextToken();
            break;
        case TOK_UNARY:
            getNextToken();
            if (!isascii(CurTok))
                return LogErrorP("Expected unary operator");
            FnName = "unary";
            FnName += (char)CurTok;
            Kind = 1;
            getNextToken();
            break;
        case TOK_BINARY:
            getNextToken();
            if (!isascii(CurTok))
                return LogErrorP("Expected binary operator\n");
            FnName = "binary";
            FnName += (char)CurTok;
            Kind = 2;
            getNextToken();

            // Read the precedence if present
            if (CurTok == TOK_NUMBER) {
                if (NumVal < 1 || NumVal > 100)
                    return LogErrorP("Invalid precedence: must be 1..100\n");
                BinaryPrecedence = (unsigned)NumVal;
                getNextToken();
            }
            break;
    }

    if (CurTok != '(')
        return LogErrorP("Expected '(' in prototype\n");
    
    std::vector<std::string> ArgNames;
    while (getNextToken() == TOK_IDENTIFIER)
        ArgNames.push_back(IdentifierStr);
    if (CurTok != ')')
        return LogErrorP("Expected ')' in prototype\n");

    // success
    getNextToken(); // eat ')'

    // Verify right number of names for operator
    if (Kind && ArgNames.size() != Kind)
        return LogErrorP("Invalid number of operands for operator\n");

    return std::make_unique<PrototypeAST>(FnName, std::move(ArgNames), Kind != 0, BinaryPrecedence);
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