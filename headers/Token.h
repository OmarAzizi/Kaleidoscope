#ifndef __TOKEN_H__
#define __TOKEN_H__

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
    TOK_IN = -10,

    // operators
    TOK_BINARY = -11,
    TOK_UNARY = -12
};

#endif