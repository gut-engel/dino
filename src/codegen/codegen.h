#ifndef CODEGEN_H
#define CODEGEN_H

#include "../ast/ast.h"
#include "../lexer/token.h"

// A class known to codegen: its name plus the names of its methods (functions)
// and fields (variables), used to resolve `ClassName.member`.
typedef struct {
    StringView name;
    StringView *members;
    bool *member_is_method;
    size_t member_count;
    size_t member_cap;
} CodegenClass;

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
    // Declared classes (top-level), for `ClassName.method(...)` resolution.
    CodegenClass *classes;
    size_t class_count;
    size_t class_cap;
} Codegen;

// Generates C source from the AST. Returns a heap-allocated string that the
// caller must free, or NULL on error.
char *codegen_generate(Arena *arena, ASTNode *program, char **error_out);

#endif
