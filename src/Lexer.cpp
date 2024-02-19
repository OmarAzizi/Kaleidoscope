#include "../headers/Lexer.h"

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
        if (IdentifierStr == "binary")
            return TOK_BINARY;
        if (IdentifierStr == "unary")
            return TOK_UNARY;

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
