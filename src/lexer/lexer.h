#ifndef LEXER_H
#define LEXER_H

#include "token.h"

typedef struct {
    const char *source;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
} Lexer;

Lexer lexer_new(const char *source, size_t length);
Token lexer_next_token(Lexer *lexer);
void lexer_print_token(Token token);

#endif