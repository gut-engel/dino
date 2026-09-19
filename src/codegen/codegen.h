#ifndef CODEGEN_H
#define CODEGEN_H

#include "../ast/ast.h"
#include "../lexer/token.h"

typedef struct {
    Arena *arena;
    StringBuilder out;
    int indent_level;
    bool had_error;
    StringBuilder error_msg;
    StringView *string_vars; // names declared/initialized as strings
    size_t string_vars_count;
    size_t string_vars_cap;
} Codegen;

// Generates C source from the AST. Returns a heap-allocated string that the
// caller must free, or NULL on error.
char *codegen_generate(Arena *arena, ASTNode *program, char **error_out);

#endif