#include "ast.h"
#include <stdio.h>

ASTNode *ast_new(Arena *arena, ASTNodeType type, size_t line, size_t column) {
    ASTNode *node = ARENA_ALLOC(arena, ASTNode);
    node->type = type;
    node->line = line;
    node->column = column;
    return node;
}

void ast_node_list_init(Arena *arena, ASTNodeList *list) {
    (void)arena;
    list->nodes = NULL;
    list->count = 0;
    list->capacity = 0;
}

void ast_node_list_push(Arena *arena, ASTNodeList *list, ASTNode *node) {
    (void)arena;
    if (list->count >= list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2 : 8;
        list->nodes = realloc(list->nodes, sizeof(ASTNode *) * new_capacity);
        list->capacity = new_capacity;
    }
    list->nodes[list->count++] = node;
}

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) printf("  ");
}

static const char *ast_node_type_name(ASTNodeType type) {
    switch (type) {
        case AST_PROGRAM: return "Program";
        case AST_VAR_DECL: return "VarDecl";

        case AST_FUNC_DECL: return "FuncDecl";
        case AST_IF_STMT: return "IfStmt";
        case AST_FOR_STMT: return "ForStmt";
        case AST_WHILE_STMT: return "WhileStmt";
        case AST_SWITCH_STMT: return "SwitchStmt";
        case AST_CASE_STMT: return "CaseStmt";
        case AST_BLOCK: return "Block";
        case AST_EXPR_STMT: return "ExprStmt";
        case AST_BINARY_EXPR: return "BinaryExpr";
        case AST_UNARY_EXPR: return "UnaryExpr";
        case AST_CALL_EXPR: return "CallExpr";
        case AST_MEMBER_EXPR: return "MemberExpr";
        case AST_IDENTIFIER: return "Identifier";
        case AST_NUMBER: return "Number";
        case AST_STRING: return "String";
        case AST_INTERPOLATED_STRING: return "InterpolatedString";
        case AST_BOOL_LITERAL: return "BoolLiteral";
        case AST_NULL_LITERAL: return "NullLiteral";
        case AST_ARRAY_LITERAL: return "ArrayLiteral";
        case AST_DICT_LITERAL: return "DictLiteral";
        case AST_INDEX_EXPR: return "IndexExpr";
        case AST_ASSIGN: return "Assign";
        case AST_TRY_STMT: return "TryStmt";
        case AST_THROW_STMT: return "ThrowStmt";
    }
    return "Unknown";
}

void ast_print(ASTNode *node, int indent) {
    if (!node) {
        print_indent(indent);
        printf("(null)\n");
        return;
    }

    print_indent(indent);
    printf("%s (line=%zu, col=%zu)\n", ast_node_type_name(node->type), node->line, node->column);

    switch (node->type) {
        case AST_PROGRAM:
            for (size_t i = 0; i < node->as.program.statements.count; i++) {
                ast_print(node->as.program.statements.nodes[i], indent + 1);
            }
            break;
        case AST_VAR_DECL:
            print_indent(indent + 1);
            printf("name: %.*s, is_const: %s\n", (int)node->as.var_decl.name.length, node->as.var_decl.name.data, node->as.var_decl.is_const ? "true" : "false");
            if (node->as.var_decl.type) {
                print_indent(indent + 1);
                printf("type:\n");
                ast_print(node->as.var_decl.type, indent + 2);
            }
            if (node->as.var_decl.initializer) {
                print_indent(indent + 1);
                printf("initializer:\n");
                ast_print(node->as.var_decl.initializer, indent + 2);
            }
            break;
        case AST_FUNC_DECL:
            print_indent(indent + 1);
            printf("name: %.*s\n", (int)node->as.func_decl.name.length, node->as.func_decl.name.data);
            for (size_t p = 0; p < node->as.func_decl.params.count; p++) {
                print_indent(indent + 1);
                printf("param:\n");
                ast_print(node->as.func_decl.params.nodes[p], indent + 2);
            }
            print_indent(indent + 1);
            printf("body:\n");
            ast_print(node->as.func_decl.body, indent + 2);
            break;
        case AST_IF_STMT:
            print_indent(indent + 1);
            printf("condition:\n");
            ast_print(node->as.if_stmt.condition, indent + 2);
            print_indent(indent + 1);
            printf("then:\n");
            ast_print(node->as.if_stmt.then_branch, indent + 2);
            if (node->as.if_stmt.else_branch) {
                print_indent(indent + 1);
                printf("else:\n");
                ast_print(node->as.if_stmt.else_branch, indent + 2);
            }
            break;
        case AST_FOR_STMT:
            if (node->as.for_stmt.init) {
                print_indent(indent + 1);
                printf("init:\n");
                ast_print(node->as.for_stmt.init, indent + 2);
            }
            if (node->as.for_stmt.condition) {
                print_indent(indent + 1);
                printf("condition:\n");
                ast_print(node->as.for_stmt.condition, indent + 2);
            }
            if (node->as.for_stmt.increment) {
                print_indent(indent + 1);
                printf("increment:\n");
                ast_print(node->as.for_stmt.increment, indent + 2);
            }
            print_indent(indent + 1);
            printf("body:\n");
            ast_print(node->as.for_stmt.body, indent + 2);
            break;
        case AST_WHILE_STMT:
            print_indent(indent + 1);
            printf("condition:\n");
            ast_print(node->as.while_stmt.condition, indent + 2);
            print_indent(indent + 1);
            printf("body:\n");
            ast_print(node->as.while_stmt.body, indent + 2);
            break;
        case AST_SWITCH_STMT:
            print_indent(indent + 1);
            printf("expression:\n");
            ast_print(node->as.switch_stmt.expression, indent + 2);
            for (size_t i = 0; i < node->as.switch_stmt.cases.count; i++) {
                ast_print(node->as.switch_stmt.cases.nodes[i], indent + 1);
            }
            if (node->as.switch_stmt.default_case) {
                print_indent(indent + 1);
                printf("default:\n");
                ast_print(node->as.switch_stmt.default_case, indent + 2);
            }
            break;
        case AST_CASE_STMT:
            if (node->as.case_stmt.condition) {
                print_indent(indent + 1);
                printf("condition:\n");
                ast_print(node->as.case_stmt.condition, indent + 2);
            } else {
                print_indent(indent + 1);
                printf("default case\n");
            }
            for (size_t i = 0; i < node->as.case_stmt.body.count; i++) {
                ast_print(node->as.case_stmt.body.nodes[i], indent + 1);
            }
            break;
        case AST_BLOCK:
            for (size_t i = 0; i < node->as.block.statements.count; i++) {
                ast_print(node->as.block.statements.nodes[i], indent + 1);
            }
            break;
        case AST_EXPR_STMT:
            print_indent(indent + 1);
            printf("expression:\n");
            ast_print(node->as.expr_stmt.expression, indent + 2);
            break;
        case AST_BINARY_EXPR:
            print_indent(indent + 1);
            printf("op: %.*s\n", (int)node->as.binary_expr.op.length, node->as.binary_expr.op.data);
            print_indent(indent + 1);
            printf("left:\n");
            ast_print(node->as.binary_expr.left, indent + 2);
            print_indent(indent + 1);
            printf("right:\n");
            ast_print(node->as.binary_expr.right, indent + 2);
            break;
        case AST_UNARY_EXPR:
            print_indent(indent + 1);
            printf("op: %.*s, prefix: %s\n", (int)node->as.unary_expr.op.length, node->as.unary_expr.op.data, node->as.unary_expr.is_prefix ? "true" : "false");
            print_indent(indent + 1);
            printf("operand:\n");
            ast_print(node->as.unary_expr.operand, indent + 2);
            break;
        case AST_CALL_EXPR:
            print_indent(indent + 1);
            printf("callee:\n");
            ast_print(node->as.call_expr.callee, indent + 2);
            for (size_t i = 0; i < node->as.call_expr.arguments.count; i++) {
                print_indent(indent + 1);
                printf("arg %zu:\n", i);
                ast_print(node->as.call_expr.arguments.nodes[i], indent + 2);
            }
            break;
        case AST_MEMBER_EXPR:
            print_indent(indent + 1);
            printf("object:\n");
            ast_print(node->as.member_expr.object, indent + 2);
            print_indent(indent + 1);
            printf("property: %.*s\n", (int)node->as.member_expr.property.length, node->as.member_expr.property.data);
            break;
        case AST_IDENTIFIER:
            print_indent(indent + 1);
            printf("name: %.*s\n", (int)node->as.identifier.name.length, node->as.identifier.name.data);
            break;
        case AST_NUMBER:
            print_indent(indent + 1);
            printf("value: %.*s\n", (int)node->as.number.value.length, node->as.number.value.data);
            break;
        case AST_STRING:
            print_indent(indent + 1);
            printf("value: %.*s\n", (int)node->as.string.value.length, node->as.string.value.data);
            break;
        case AST_INTERPOLATED_STRING:
            for (size_t i = 0; i < node->as.interpolated_string.parts.count; i++) {
                print_indent(indent + 1);
                printf("part %zu:\n", i);
                ast_print(node->as.interpolated_string.parts.nodes[i], indent + 2);
            }
            break;
        case AST_BOOL_LITERAL:
            print_indent(indent + 1);
            printf("value: %s\n", node->as.bool_literal.value ? "true" : "false");
            break;
        case AST_NULL_LITERAL:
            print_indent(indent + 1);
            printf("null\n");
            break;
        case AST_ARRAY_LITERAL:
            for (size_t i = 0; i < node->as.array_literal.elements.count; i++) {
                print_indent(indent + 1);
                printf("element %zu:\n", i);
                ast_print(node->as.array_literal.elements.nodes[i], indent + 2);
            }
            break;
        case AST_DICT_LITERAL:
            for (size_t i = 0; i < node->as.dict_literal.keys.count; i++) {
                print_indent(indent + 1);
                printf("entry %zu key:\n", i);
                ast_print(node->as.dict_literal.keys.nodes[i], indent + 2);
                print_indent(indent + 1);
                printf("entry %zu value:\n", i);
                ast_print(node->as.dict_literal.values.nodes[i], indent + 2);
            }
            break;
        case AST_INDEX_EXPR:
            print_indent(indent + 1);
            printf("object:\n");
            ast_print(node->as.index_expr.object, indent + 2);
            print_indent(indent + 1);
            printf("index:\n");
            ast_print(node->as.index_expr.index, indent + 2);
            break;
        case AST_ASSIGN:
            print_indent(indent + 1);
            printf("target:\n");
            ast_print(node->as.assign.target, indent + 2);
            print_indent(indent + 1);
            printf("value:\n");
            ast_print(node->as.assign.value, indent + 2);
            break;
        case AST_TRY_STMT:
            print_indent(indent + 1);
            printf("try:\n");
            ast_print(node->as.try_stmt.try_body, indent + 2);
            print_indent(indent + 1);
            printf("catch (%.*s):\n", (int)node->as.try_stmt.catch_name.length, node->as.try_stmt.catch_name.data);
            ast_print(node->as.try_stmt.catch_body, indent + 2);
            break;
        case AST_THROW_STMT:
            print_indent(indent + 1);
            printf("value:\n");
            ast_print(node->as.throw_stmt.value, indent + 2);
            break;
    }
}