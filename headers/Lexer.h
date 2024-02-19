#ifndef __LEXER_H__
#define __LEXER_H__

#include "Token.h"
#include "common.h"

int gettok();

/*
    Each token returned by our lexer will either be one of the Token enum values 
    or it will be an ‘unknown’ character like ‘+’, which is returned as its ASCII value. 

    If the current token is an identifier, the IdentifierStr global variable holds the name of the identifier. 
    If the current token is a numeric literal (like 1.0), NumVal holds its value.
*/

static std::string IdentifierStr; // Filled in if TOK_IDENTIFIER
static double NumVal;             // Filled in if TOK_NUMBER

#endif