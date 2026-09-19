#ifndef PARSER_H
#define PARSER_H

#include "../lexer/lexer.h"
#include "../ast/ast.h"

typedef struct {
    Lexer lexer;
    Token current;
    Token previous;
    bool had_error;
    bool panic_mode;
    bool quiet; // suppress printed diagnostics (used by sub-parsers)
    size_t error_count;
    Arena *arena;
} Parser;

Parser parser_new(const char *source, size_t length, Arena *arena);
ASTNode *parser_parse(Parser *parser);
void parser_print_errors(Parser *parser);

#endif