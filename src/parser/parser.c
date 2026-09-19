#include "parser.h"
#include <ctype.h>

Parser parser_new(const char *source, size_t length, Arena *arena) {
    Parser parser;
    parser.lexer = lexer_new(source, length);
    parser.current = lexer_next_token(&parser.lexer);
    parser.previous = (Token){.type = TOKEN_EOF, .lexeme = sv_from_cstr(""), .line = 0, .column = 0};
    parser.had_error = false;
    parser.panic_mode = false;
    parser.arena = arena;
    return parser;
}

static void advance(Parser *parser) {
    parser->previous = parser->current;
    if (parser->current.type != TOKEN_EOF) {
        parser->current = lexer_next_token(&parser->lexer);
    }
}

static void error_at(Parser *parser, Token token, const char *message) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    parser->had_error = true;

    fprintf(stderr, "[line %zu, col %zu] Error", token.line, token.column);
    if (token.type == TOKEN_EOF) {
        fprintf(stderr, " at end");
    } else if (token.type == TOKEN_ERROR) {
        fprintf(stderr, " at '%.*s'", (int)token.lexeme.length, token.lexeme.data);
    } else {
        fprintf(stderr, " at '%.*s'", (int)token.lexeme.length, token.lexeme.data);
    }
    fprintf(stderr, ": %s\n", message);
}

static void error_current(Parser *parser, const char *message) {
    error_at(parser, parser->current, message);
}

static bool check(Parser *parser, TokenType type) {
    return parser->current.type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance(parser);
    return true;
}

static bool match_any(Parser *parser, size_t count, TokenType types[]) {
    for (size_t i = 0; i < count; i++) {
        if (check(parser, types[i])) {
            advance(parser);
            return true;
        }
    }
    return false;
}

static bool consume(Parser *parser, TokenType type, const char *message) {
    if (check(parser, type)) {
        advance(parser);
        return true;
    }
    error_current(parser, message);
    return false;
}

static void synchronize(Parser *parser) {
    parser->panic_mode = false;
    while (parser->current.type != TOKEN_EOF) {
        switch (parser->current.type) {
            case TOKEN_SEMICOLON:
                advance(parser);
                return;
            case TOKEN_VAR:
            case TOKEN_CONST:
            case TOKEN_IF:
            case TOKEN_FOR:
            case TOKEN_WHILE:
            case TOKEN_SWITCH:
            case TOKEN_RBRACE:
                return;
            default:
                advance(parser);
        }
    }
}

// Forward declarations
static ASTNode *expression(Parser *parser);
static ASTNode *statement(Parser *parser);
static ASTNode *declaration(Parser *parser);
static ASTNode *block(Parser *parser);
static ASTNode *call(Parser *parser);

static bool is_type_token(TokenType type) {
    return type == TOKEN_BOOL || type == TOKEN_INT || type == TOKEN_FLOAT || type == TOKEN_VOID;
}

static ASTNode *type_node(Parser *parser) {
    Token type_token = parser->previous;
    ASTNode *node = ast_new(parser->arena, AST_IDENTIFIER, type_token.line, type_token.column);
    node->as.identifier.name = type_token.lexeme;
    return node;
}

static ASTNode *primary(Parser *parser) {
    if (match(parser, TOKEN_TRUE)) {
        ASTNode *node = ast_new(parser->arena, AST_BOOL_LITERAL, parser->previous.line, parser->previous.column);
        node->as.bool_literal.value = true;
        return node;
    }
    if (match(parser, TOKEN_FALSE)) {
        ASTNode *node = ast_new(parser->arena, AST_BOOL_LITERAL, parser->previous.line, parser->previous.column);
        node->as.bool_literal.value = false;
        return node;
    }
    if (match(parser, TOKEN_NUMBER)) {
        ASTNode *node = ast_new(parser->arena, AST_NUMBER, parser->previous.line, parser->previous.column);
        node->as.number.value = parser->previous.lexeme;
        return node;
    }
    if (match(parser, TOKEN_STRING)) {
        ASTNode *node = ast_new(parser->arena, AST_STRING, parser->previous.line, parser->previous.column);
        node->as.string.value = parser->previous.lexeme;
        return node;
    }
    if (match(parser, TOKEN_INTERPOLATED_STRING)) {
        ASTNode *node = ast_new(parser->arena, AST_INTERPOLATED_STRING, parser->previous.line, parser->previous.column);
        node->as.interpolated_string.value = parser->previous.lexeme;
        return node;
    }
    if (match(parser, TOKEN_IDENTIFIER) || match(parser, TOKEN_CONSOLE)) {
        ASTNode *node = ast_new(parser->arena, AST_IDENTIFIER, parser->previous.line, parser->previous.column);
        node->as.identifier.name = parser->previous.lexeme;
        return node;
    }
    if (match(parser, TOKEN_LPAREN)) {
        ASTNode *expr = expression(parser);
        consume(parser, TOKEN_RPAREN, "Expect ')' after expression.");
        return expr;
    }
    error_current(parser, "Expect expression.");
    return NULL;
}

static ASTNode *unary(Parser *parser) {
    if (match_any(parser, 2, (TokenType[]){TOKEN_MINUS, TOKEN_BANG})) {
        Token op = parser->previous;
        ASTNode *operand = unary(parser);
        ASTNode *node = ast_new(parser->arena, AST_UNARY_EXPR, op.line, op.column);
        node->as.unary_expr.op = op.lexeme;
        node->as.unary_expr.operand = operand;
        node->as.unary_expr.is_prefix = true;
        return node;
    }
    return call(parser);
}

static ASTNode *multiplicative(Parser *parser) {
    ASTNode *left = unary(parser);
    while (match_any(parser, 3, (TokenType[]){TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT})) {
        Token op = parser->previous;
        ASTNode *right = unary(parser);
        ASTNode *node = ast_new(parser->arena, AST_BINARY_EXPR, op.line, op.column);
        node->as.binary_expr.op = op.lexeme;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

static ASTNode *additive(Parser *parser) {
    ASTNode *left = multiplicative(parser);
    while (match_any(parser, 2, (TokenType[]){TOKEN_PLUS, TOKEN_MINUS})) {
        Token op = parser->previous;
        ASTNode *right = multiplicative(parser);
        ASTNode *node = ast_new(parser->arena, AST_BINARY_EXPR, op.line, op.column);
        node->as.binary_expr.op = op.lexeme;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

static ASTNode *relational(Parser *parser) {
    ASTNode *left = additive(parser);
    while (match_any(parser, 4, (TokenType[]){TOKEN_LT, TOKEN_LT_EQ, TOKEN_GT, TOKEN_GT_EQ})) {
        Token op = parser->previous;
        ASTNode *right = additive(parser);
        ASTNode *node = ast_new(parser->arena, AST_BINARY_EXPR, op.line, op.column);
        node->as.binary_expr.op = op.lexeme;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

static ASTNode *equality(Parser *parser) {
    ASTNode *left = relational(parser);
    while (match_any(parser, 2, (TokenType[]){TOKEN_EQ_EQ, TOKEN_BANG_EQ})) {
        Token op = parser->previous;
        ASTNode *right = relational(parser);
        ASTNode *node = ast_new(parser->arena, AST_BINARY_EXPR, op.line, op.column);
        node->as.binary_expr.op = op.lexeme;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

static ASTNode *logical_and(Parser *parser) {
    ASTNode *left = equality(parser);
    while (match(parser, TOKEN_AND_AND)) {
        Token op = parser->previous;
        ASTNode *right = equality(parser);
        ASTNode *node = ast_new(parser->arena, AST_BINARY_EXPR, op.line, op.column);
        node->as.binary_expr.op = op.lexeme;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

static ASTNode *logical_or(Parser *parser) {
    ASTNode *left = logical_and(parser);
    while (match(parser, TOKEN_OR_OR)) {
        Token op = parser->previous;
        ASTNode *right = logical_and(parser);
        ASTNode *node = ast_new(parser->arena, AST_BINARY_EXPR, op.line, op.column);
        node->as.binary_expr.op = op.lexeme;
        node->as.binary_expr.left = left;
        node->as.binary_expr.right = right;
        left = node;
    }
    return left;
}

static ASTNode *call(Parser *parser) {
    ASTNode *callee = primary(parser);
    if (!callee) return NULL;
    while (true) {
        if (match(parser, TOKEN_DOT)) {
            Token prop = parser->current;
            advance(parser);
            ASTNode *member = ast_new(parser->arena, AST_MEMBER_EXPR, prop.line, prop.column);
            member->as.member_expr.object = callee;
            member->as.member_expr.property = prop.lexeme;
            callee = member;
        } else if (match(parser, TOKEN_LPAREN)) {
            ASTNode *call_node = ast_new(parser->arena, AST_CALL_EXPR, callee->line, callee->column);
            call_node->as.call_expr.callee = callee;
            ast_node_list_init(parser->arena, &call_node->as.call_expr.arguments);
            if (!check(parser, TOKEN_RPAREN)) {
                do {
                    ast_node_list_push(parser->arena, &call_node->as.call_expr.arguments, expression(parser));
                } while (match(parser, TOKEN_COMMA));
            }
            consume(parser, TOKEN_RPAREN, "Expect ')' after arguments.");
            callee = call_node;
        } else if (match(parser, TOKEN_PLUS_PLUS) || match(parser, TOKEN_MINUS_MINUS)) {
            Token op = parser->previous;
            ASTNode *inc = ast_new(parser->arena, AST_UNARY_EXPR, op.line, op.column);
            inc->as.unary_expr.op = op.lexeme;
            inc->as.unary_expr.operand = callee;
            inc->as.unary_expr.is_prefix = false;
            callee = inc;
        } else {
            break;
        }
    }
    return callee;
}

static ASTNode *expression(Parser *parser) {
    return logical_or(parser);
}

// Variable declaration: [const|var] [type]? name = initializer ;
static ASTNode *var_declaration(Parser *parser) {
    Token keyword = parser->previous; // const or var
    bool is_const = keyword.type == TOKEN_CONST;

    Token name_token;
    ASTNode *type_node_val = NULL;

    if (is_type_token(parser->current.type)) {
        advance(parser);
        type_node_val = type_node(parser);
    }

    if (check(parser, TOKEN_IDENTIFIER)) {
        name_token = parser->current;
        advance(parser);
    } else {
        error_current(parser, "Expect variable name");
        synchronize(parser);
        return NULL;
    }

    ASTNode *initializer = NULL;
    if (match(parser, TOKEN_EQ)) {
        initializer = expression(parser);
    }

    // Optional semicolon (the language allows both, like the example shows '};')
    match(parser, TOKEN_SEMICOLON);

    ASTNode *node = ast_new(parser->arena, AST_VAR_DECL, keyword.line, keyword.column);
    node->as.var_decl.name = name_token.lexeme;
    node->as.var_decl.type = type_node_val;
    node->as.var_decl.initializer = initializer;
    node->as.var_decl.is_const = is_const;
    return node;
}

static ASTNode *if_statement(Parser *parser) {
    Token keyword = parser->previous; // 'if'
    consume(parser, TOKEN_LPAREN, "Expect '(' after 'if'.");
    ASTNode *condition = expression(parser);
    consume(parser, TOKEN_RPAREN, "Expect ')' after if condition.");

    ASTNode *then_branch = block(parser);
    ASTNode *else_branch = NULL;

    if (match(parser, TOKEN_ELSE)) {
        if (check(parser, TOKEN_IF)) {
            advance(parser);
            else_branch = if_statement(parser);
        } else {
            else_branch = block(parser);
        }
    }

    match(parser, TOKEN_SEMICOLON); // optional trailing ';' after '}' e.g. '};'

    ASTNode *node = ast_new(parser->arena, AST_IF_STMT, keyword.line, keyword.column);
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
    return node;
}

static ASTNode *for_statement(Parser *parser) {
    Token keyword = parser->previous; // 'for'
    consume(parser, TOKEN_LPAREN, "Expect '(' after 'for'.");

    ASTNode *init = NULL;
    ASTNode *condition = NULL;
    ASTNode *increment = NULL;

    // init: var declaration or expression
    if (match(parser, TOKEN_VAR)) {
        init = var_declaration(parser); // consumes its own ';'
    } else if (match(parser, TOKEN_CONST)) {
        init = var_declaration(parser);
    } else if (!check(parser, TOKEN_SEMICOLON)) {
        init = expression(parser);
        consume(parser, TOKEN_SEMICOLON, "Expect ';' after for init.");
    } else {
        advance(parser); // skip ';'
    }

    // condition
    if (!check(parser, TOKEN_SEMICOLON)) {
        condition = expression(parser);
        consume(parser, TOKEN_SEMICOLON, "Expect ';' after for condition.");
    } else {
        advance(parser);
    }

    // increment
    if (!check(parser, TOKEN_RPAREN)) {
        increment = expression(parser);
    }
    consume(parser, TOKEN_RPAREN, "Expect ')' after for clauses.");

    ASTNode *body = block(parser);
    match(parser, TOKEN_SEMICOLON); // optional trailing ';'

    ASTNode *node = ast_new(parser->arena, AST_FOR_STMT, keyword.line, keyword.column);
    node->as.for_stmt.init = init;
    node->as.for_stmt.condition = condition;
    node->as.for_stmt.increment = increment;
    node->as.for_stmt.body = body;
    return node;
}

static ASTNode *while_statement(Parser *parser) {
    Token keyword = parser->previous; // 'while'
    consume(parser, TOKEN_LPAREN, "Expect '(' after 'while'.");
    ASTNode *condition = expression(parser);
    consume(parser, TOKEN_RPAREN, "Expect ')' after while condition.");

    ASTNode *body = block(parser);
    match(parser, TOKEN_SEMICOLON);

    ASTNode *node = ast_new(parser->arena, AST_WHILE_STMT, keyword.line, keyword.column);
    node->as.while_stmt.condition = condition;
    node->as.while_stmt.body = body;
    return node;
}

static ASTNode *case_statement(Parser *parser) {
    Token keyword = parser->previous; // 'case' or 'default'
    bool is_default = keyword.type == TOKEN_DEFAULT;
    ASTNode *condition = NULL;

    if (!is_default) {
        consume(parser, TOKEN_LPAREN, "Expect '(' after 'case'.");
        condition = expression(parser);
        consume(parser, TOKEN_RPAREN, "Expect ')' after case condition.");
    }

    consume(parser, TOKEN_LBRACE, "Expect '{' to start case body.");

    ASTNode *node = ast_new(parser->arena, AST_CASE_STMT, keyword.line, keyword.column);
    node->as.case_stmt.condition = condition;
    ast_node_list_init(parser->arena, &node->as.case_stmt.body);

    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF) && !parser->panic_mode) {
        ast_node_list_push(parser->arena, &node->as.case_stmt.body, statement(parser));
    }
    consume(parser, TOKEN_RBRACE, "Expect '}' after case body.");
    match(parser, TOKEN_SEMICOLON); // optional trailing ';' e.g. '};'

    return node;
}

static ASTNode *switch_statement(Parser *parser) {
    Token keyword = parser->previous; // 'switch'
    consume(parser, TOKEN_LPAREN, "Expect '(' after 'switch'.");
    ASTNode *expression_node = expression(parser);
    consume(parser, TOKEN_RPAREN, "Expect ')' after switch expression.");

    consume(parser, TOKEN_LBRACE, "Expect '{' to start switch block.");

    ASTNode *node = ast_new(parser->arena, AST_SWITCH_STMT, keyword.line, keyword.column);
    node->as.switch_stmt.expression = expression_node;
    node->as.switch_stmt.default_case = NULL;
    ast_node_list_init(parser->arena, &node->as.switch_stmt.cases);

    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF) && !parser->panic_mode) {
        if (match(parser, TOKEN_CASE)) {
            ast_node_list_push(parser->arena, &node->as.switch_stmt.cases, case_statement(parser));
        } else if (match(parser, TOKEN_DEFAULT)) {
            node->as.switch_stmt.default_case = case_statement(parser);
        } else {
            error_current(parser, "Expect 'case' or 'default' in switch statement.");
            synchronize(parser);
        }
    }
    consume(parser, TOKEN_RBRACE, "Expect '}' after switch block.");
    match(parser, TOKEN_SEMICOLON); // optional trailing ';'

    return node;
}

static ASTNode *block(Parser *parser) {
    consume(parser, TOKEN_LBRACE, "Expect '{'.");
    ASTNode *node = ast_new(parser->arena, AST_BLOCK, parser->previous.line, parser->previous.column);
    ast_node_list_init(parser->arena, &node->as.block.statements);

    while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF) && !parser->panic_mode) {
        ast_node_list_push(parser->arena, &node->as.block.statements, declaration(parser));
    }
    consume(parser, TOKEN_RBRACE, "Expect '}' after block.");
    return node;
}

static ASTNode *expression_statement(Parser *parser) {
    Token start = parser->previous;
    ASTNode *expr = expression(parser);
    match(parser, TOKEN_SEMICOLON); // optional ';'

    ASTNode *node = ast_new(parser->arena, AST_EXPR_STMT, start.line, start.column);
    node->as.expr_stmt.expression = expr;
    return node;
}

static ASTNode *statement(Parser *parser) {
    if (match(parser, TOKEN_IF)) return if_statement(parser);
    if (match(parser, TOKEN_FOR)) return for_statement(parser);
    if (match(parser, TOKEN_WHILE)) return while_statement(parser);
    if (match(parser, TOKEN_SWITCH)) return switch_statement(parser);
    if (match(parser, TOKEN_LBRACE)) {
        // A bare block statement - block() already consumed '{'
        ASTNode *node = ast_new(parser->arena, AST_BLOCK, parser->previous.line, parser->previous.column);
        ast_node_list_init(parser->arena, &node->as.block.statements);

        while (!check(parser, TOKEN_RBRACE) && !check(parser, TOKEN_EOF) && !parser->panic_mode) {
            ast_node_list_push(parser->arena, &node->as.block.statements, declaration(parser));
        }
        consume(parser, TOKEN_RBRACE, "Expect '}' after block.");
        return node;
    }
    if (match(parser, TOKEN_BREAK)) {
        // Treat as an expression statement referencing the keyword name for codegen simplicity
        match(parser, TOKEN_SEMICOLON);
        ASTNode *node = ast_new(parser->arena, AST_EXPR_STMT, parser->previous.line, parser->previous.column);
        ASTNode *id = ast_new(parser->arena, AST_IDENTIFIER, parser->previous.line, parser->previous.column);
        id->as.identifier.name = sv_from_cstr("break");
        node->as.expr_stmt.expression = id;
        return node;
    }
    if (match(parser, TOKEN_CONTINUE)) {
        match(parser, TOKEN_SEMICOLON);
        ASTNode *node = ast_new(parser->arena, AST_EXPR_STMT, parser->previous.line, parser->previous.column);
        ASTNode *id = ast_new(parser->arena, AST_IDENTIFIER, parser->previous.line, parser->previous.column);
        id->as.identifier.name = sv_from_cstr("continue");
        node->as.expr_stmt.expression = id;
        return node;
    }
    return expression_statement(parser);
}

static ASTNode *declaration(Parser *parser) {
    if (match(parser, TOKEN_VAR) || match(parser, TOKEN_CONST)) {
        ASTNode *node = var_declaration(parser);
        if (parser->panic_mode) synchronize(parser);
        return node;
    }
    return statement(parser);
}

ASTNode *parser_parse(Parser *parser) {
    ASTNode *program = ast_new(parser->arena, AST_PROGRAM, 0, 0);
    ast_node_list_init(parser->arena, &program->as.program.statements);

    while (!check(parser, TOKEN_EOF) && !parser->panic_mode) {
        ASTNode *stmt = declaration(parser);
        if (stmt) {
            ast_node_list_push(parser->arena, &program->as.program.statements, stmt);
        }
        if (parser->panic_mode) synchronize(parser);
    }

    // EOF is fine as-is
    return program;
}

void parser_print_errors(Parser *parser) {
    (void)parser; // Errors are printed as they occur
}