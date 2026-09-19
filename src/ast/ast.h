#ifndef AST_H
#define AST_H

#include "../common.h"

typedef struct ASTNode ASTNode;
typedef struct ASTNodeList ASTNodeList;

typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_FUNC_DECL,
    AST_IF_STMT,
    AST_FOR_STMT,
    AST_WHILE_STMT,
    AST_SWITCH_STMT,
    AST_CASE_STMT,
    AST_BLOCK,
    AST_EXPR_STMT,
    AST_BINARY_EXPR,
    AST_UNARY_EXPR,
    AST_CALL_EXPR,
    AST_MEMBER_EXPR,
    AST_IDENTIFIER,
    AST_NUMBER,
    AST_STRING,
    AST_INTERPOLATED_STRING,
    AST_BOOL_LITERAL,
} ASTNodeType;

struct ASTNodeList {
    ASTNode **nodes;
    size_t count;
    size_t capacity;
};

typedef struct {
    StringView name;
    ASTNode *type; // Can be NULL for inferred types
    ASTNode *initializer;
    bool is_const;
} VarDecl;

typedef struct {
    StringView name;
    ASTNodeList params; // AST_VAR_DECL nodes (type + name, no initializer)
    ASTNode *body;      // AST_BLOCK
} FuncDecl;

typedef struct {
    ASTNode *condition;
    ASTNode *then_branch;
    ASTNode *else_branch; // Can be NULL
} IfStmt;

typedef struct {
    ASTNode *init;        // Can be NULL
    ASTNode *condition;   // Can be NULL
    ASTNode *increment;   // Can be NULL
    ASTNode *body;
} ForStmt;

typedef struct {
    ASTNode *condition;
    ASTNode *body;
} WhileStmt;

typedef struct {
    ASTNode *expression;
    ASTNodeList cases;
    ASTNode *default_case; // Can be NULL
} SwitchStmt;

typedef struct {
    ASTNode *condition; // NULL for default case
    ASTNodeList body;
} CaseStmt;

struct ASTNode {
    ASTNodeType type;
    size_t line;
    size_t column;
    union {
        struct { ASTNodeList statements; } program;
        VarDecl var_decl;
        FuncDecl func_decl;
        IfStmt if_stmt;
        ForStmt for_stmt;
        WhileStmt while_stmt;
        SwitchStmt switch_stmt;
        CaseStmt case_stmt;
        struct { ASTNodeList statements; } block;
        struct { ASTNode *expression; } expr_stmt;
        struct { StringView op; ASTNode *left; ASTNode *right; } binary_expr;
        struct { StringView op; ASTNode *operand; bool is_prefix; } unary_expr;
        struct { ASTNode *callee; ASTNodeList arguments; } call_expr;
        struct { ASTNode *object; StringView property; } member_expr;
        struct { StringView name; } identifier;
        struct { StringView value; } number;
        struct { StringView value; } string;
        struct { StringView value; } interpolated_string;
        struct { bool value; } bool_literal;
    } as;
};

ASTNode *ast_new(Arena *arena, ASTNodeType type, size_t line, size_t column);
void ast_node_list_init(Arena *arena, ASTNodeList *list);
void ast_node_list_push(Arena *arena, ASTNodeList *list, ASTNode *node);
void ast_print(ASTNode *node, int indent);

#endif