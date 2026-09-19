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
    // Lexical scope tracking: validates that value-context identifiers
    // reference a declared variable/parameter (or a known built-in).
    StringView *scope_names;
    size_t scope_count;
    size_t scope_cap;
    size_t *scope_marks;
    size_t scope_marks_count;
    size_t scope_marks_cap;
} Codegen;

// Generates C source from the AST. Returns a heap-allocated string that the
// caller must free, or NULL on error.
char *codegen_generate(Arena *arena, ASTNode *program, char **error_out);

#endif
