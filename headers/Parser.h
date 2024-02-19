#ifndef __PARSER_H__
#define __PARSER_H__

#include "common.h"
#include "Lexer.h"
#include "AST.h"

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

static int GetTokPrecedence();

std::unique_ptr<ExprAST> LogError(const char*);

std::unique_ptr<PrototypeAST> LogErrorP(const char*);

static std::unique_ptr<ExprAST> ParseNumberExpr();

static std::unique_ptr<ExprAST> ParseParenExpr(); 

static std::unique_ptr<ExprAST> ParseIdentifierExpr();

static std::unique_ptr<ExprAST> ParsePrimary();

static std::unique_ptr<ExprAST> ParseUnary();

static std::unique_ptr<ExprAST> ParseBinOpRHS(int, std::unique_ptr<ExprAST>);

static std::unique_ptr<ExprAST> ParseExpression();

static std::unique_ptr<PrototypeAST> ParsePrototype();

static std::unique_ptr<FunctionAST> ParseDefinition();

static std::unique_ptr<FunctionAST> ParseTopLevelExpr();

static std::unique_ptr<PrototypeAST> ParseExtern();

static std::unique_ptr<ExprAST> ParseIfExpr();

static std::unique_ptr<ExprAST> ParseForExpr();

#endif