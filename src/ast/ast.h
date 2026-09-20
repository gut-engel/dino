#ifndef AST_H
#define AST_H

#include "../common.h"

typedef struct ASTNode ASTNode;
typedef struct ASTNodeList ASTNodeList;

typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_FUNC_DECL,
    AST_CLASS_DECL,
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
    AST_NULL_LITERAL,
    AST_ARRAY_LITERAL,
    AST_DICT_LITERAL,
    AST_INDEX_EXPR,
    AST_ASSIGN,
    AST_TRY_STMT,
    AST_THROW_STMT,
    AST_DELETE_STMT,
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

// A class is a named namespace of functions (and optional fields). Members are
// AST_FUNC_DECL (methods) and AST_VAR_DECL (fields) nodes.
typedef struct {
    StringView name;
    ASTNodeList members;
    bool is_const;
} ClassDecl;

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
    ASTNode *else_body; // Can be NULL (`while (c) {...} else {...}`)
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

typedef struct {
    ASTNodeList elements;
} ArrayLiteral;

typedef struct {
    ASTNodeList keys;
    ASTNodeList values;
} DictLiteral;

typedef struct {
    ASTNode *object;
    ASTNode *index;
} IndexExpr;

typedef struct {
    ASTNode *target;
    ASTNode *value;
} Assign;

typedef struct {
    ASTNode *try_body;      // AST_BLOCK
    StringView catch_name;
    ASTNode *catch_body;    // AST_BLOCK
} TryStmt;

typedef struct {
    ASTNode *value;
} ThrowStmt;

typedef struct {
    ASTNode *target; // AST_INDEX_EXPR: the container[index] to delete
} DeleteStmt;

struct ASTNode {
    ASTNodeType type;
    size_t line;
    size_t column;
    union {
        struct { ASTNodeList statements; } program;
        VarDecl var_decl;
        FuncDecl func_decl;
        ClassDecl class_decl;
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
        struct { ASTNodeList parts; } interpolated_string;
        struct { bool value; } bool_literal;
        ArrayLiteral array_literal;
        DictLiteral dict_literal;
        IndexExpr index_expr;
        Assign assign;
        TryStmt try_stmt;
        ThrowStmt throw_stmt;
        DeleteStmt delete_stmt;
    } as;
};

ASTNode *ast_new(Arena *arena, ASTNodeType type, size_t line, size_t column);
void ast_node_list_init(Arena *arena, ASTNodeList *list);
void ast_node_list_push(Arena *arena, ASTNodeList *list, ASTNode *node);
void ast_print(ASTNode *node, int indent);

#endif