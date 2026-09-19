#include "parser.h"

Parser parser_new(const char *source, size_t length, Arena *arena) {
    Parser parser;
    parser.lexer = lexer_new(source, length);
    parser.current = lexer_next_token(&parser.lexer);
    parser.previous = (Token){.type = TOKEN_EOF, .lexeme = sv_from_cstr(""), .line = 0, .column = 0};
    parser.had_error = false;
    parser.panic_mode = false;
    parser.quiet = false;
    parser.error_count = 0;
    parser.arena = arena;
    return parser;
}

static void advance(Parser *parser) {
    parser->previous = parser->current;
    if (parser->current.type != TOKEN_EOF) {
        parser->current = lexer_next_token(&parser->lexer);
    }
}

// One-token lookahead. The lexer is a plain value, so a copy can be advanced
// without disturbing the parser.
static Token peek(Parser *parser) {
    Lexer copy = parser->lexer;
    return lexer_next_token(&copy);
}

static void error_at(Parser *parser, Token token, const char *message) {
    if (parser->panic_mode) return;
    parser->panic_mode = true;
    parser->had_error = true;
    parser->error_count++;

    if (parser->quiet) return; // sub-parsers just flag the error

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

// Split an interpolated string token (e.g. $"a{b}c") into AST parts: quoted
// AST_STRING text nodes alternating with parsed expressions. Parsing the inner
// expressions here means codegen sees real AST nodes (with the dynamic value
// runtime) rather than raw source text.
static void parse_interpolation(Parser *parser, StringView sv, ASTNode *node) {
    Arena *arena = parser->arena;
    ast_node_list_init(arena, &node->as.interpolated_string.parts);

    StringBuilder text = sb_new();
    size_t i = 2; // skip '$' and the opening quote
    while (i < sv.length - 1) { // stop before the closing quote
        if (sv.data[i] == '{') {
            // Flush accumulated text as a quoted string literal node.
            size_t tlen = text.length;
            char *buf = ARENA_ALLOC_ARRAY(arena, char, tlen + 2);
            buf[0] = '"';
            if (tlen) memcpy(buf + 1, text.data, tlen);
            buf[tlen + 1] = '"';
            ASTNode *txt = ast_new(arena, AST_STRING, parser->previous.line, parser->previous.column);
            txt->as.string.value = sv_from_parts(buf, tlen + 2);
            ast_node_list_push(arena, &node->as.interpolated_string.parts, txt);
            text.length = 0;

            i++;
            size_t start = i;
            while (i < sv.length - 1 && sv.data[i] != '}') i++;
            StringView expr_src = sv_from_parts(sv.data + start, i - start);

            Parser sub = parser_new(expr_src.data, expr_src.length, arena);
            sub.quiet = true;
            ASTNode *expr = expression(&sub);
            if (sub.had_error || sub.current.type != TOKEN_EOF) {
                // Report at the interpolated string in the real source; the
                // sub-parser's own (sub-string-relative) message is suppressed.
                error_at(parser, parser->previous, "Invalid expression inside interpolated string.");
                // Recover with null so codegen can keep going.
                expr = ast_new(arena, AST_NULL_LITERAL, parser->previous.line, parser->previous.column);
            }
            ast_node_list_push(arena, &node->as.interpolated_string.parts, expr);
            i++; // skip '}'
        } else {
            sb_append_char(&text, sv.data[i]);
            i++;
        }
    }

    // Trailing text.
    size_t tlen = text.length;
    char *buf = ARENA_ALLOC_ARRAY(arena, char, tlen + 2);
    buf[0] = '"';
    if (tlen) memcpy(buf + 1, text.data, tlen);
    buf[tlen + 1] = '"';
    ASTNode *txt = ast_new(arena, AST_STRING, parser->previous.line, parser->previous.column);
    txt->as.string.value = sv_from_parts(buf, tlen + 2);
    ast_node_list_push(arena, &node->as.interpolated_string.parts, txt);
    sb_free(&text);
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
    if (match(parser, TOKEN_NULL)) {
        return ast_new(parser->arena, AST_NULL_LITERAL, parser->previous.line, parser->previous.column);
    }
    if (match(parser, TOKEN_LBRACKET)) {
        // Array literal: [a, b, c]
        ASTNode *node = ast_new(parser->arena, AST_ARRAY_LITERAL, parser->previous.line, parser->previous.column);
        ast_node_list_init(parser->arena, &node->as.array_literal.elements);
        if (!check(parser, TOKEN_RBRACKET)) {
            do {
                ast_node_list_push(parser->arena, &node->as.array_literal.elements, expression(parser));
            } while (match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_RBRACKET, "Expect ']' after array elements.");
        return node;
    }
    if (match(parser, TOKEN_LBRACE)) {
        // Dictionary literal: {"key": value, ...}. Only reachable in expression
        // context — a '{' at the start of a statement is parsed as a block.
        ASTNode *node = ast_new(parser->arena, AST_DICT_LITERAL, parser->previous.line, parser->previous.column);
        ast_node_list_init(parser->arena, &node->as.dict_literal.keys);
        ast_node_list_init(parser->arena, &node->as.dict_literal.values);
        if (!check(parser, TOKEN_RBRACE)) {
            do {
                ASTNode *key = expression(parser);
                consume(parser, TOKEN_COLON, "Expect ':' after dictionary key.");
                ASTNode *value = expression(parser);
                ast_node_list_push(parser->arena, &node->as.dict_literal.keys, key);
                ast_node_list_push(parser->arena, &node->as.dict_literal.values, value);
            } while (match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_RBRACE, "Expect '}' after dictionary entries.");
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
        parse_interpolation(parser, parser->previous.lexeme, node);
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
    // Prefix ++ / -- (the postfix forms are handled in `call`). Both yield the
    // new value; codegen mutates the operand in place via _dino_inc/_dino_dec.
    if (match_any(parser, 2, (TokenType[]){TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS})) {
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
        } else if (match(parser, TOKEN_LBRACKET)) {
            ASTNode *index = expression(parser);
            consume(parser, TOKEN_RBRACKET, "Expect ']' after index.");
            ASTNode *node = ast_new(parser->arena, AST_INDEX_EXPR, callee->line, callee->column);
            node->as.index_expr.object = callee;
            node->as.index_expr.index = index;
            callee = node;
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

// Assignment is the lowest-precedence, right-associative operator so that
// `a = b = c` and `xs[i] = v` parse. `=` is only valid for lvalue targets;
// that is enforced during codegen.
static ASTNode *assignment(Parser *parser) {
    ASTNode *left = logical_or(parser);
    if (match(parser, TOKEN_EQ)) {
        Token op = parser->previous;
        ASTNode *right = assignment(parser);
        ASTNode *node = ast_new(parser->arena, AST_ASSIGN, op.line, op.column);
        node->as.assign.target = left;
        node->as.assign.value = right;
        return node;
    }
    return left;
}

static ASTNode *expression(Parser *parser) {
    return assignment(parser);
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
        // Optional '[]' array suffix, e.g. `int[] xs`.
        if (match(parser, TOKEN_LBRACKET)) {
            consume(parser, TOKEN_RBRACKET, "Expect ']' after '[' in array type.");
        }
    } else if (check(parser, TOKEN_IDENTIFIER) && peek(parser).type == TOKEN_IDENTIFIER) {
        // Identifier-based type name (string, array, dict, or a user type)
        // followed by the variable name. These names are not reserved, so
        // `const string array = ...` still works with `array` as the name.
        advance(parser);
        type_node_val = type_node(parser);
        if (match(parser, TOKEN_LBRACKET)) {
            consume(parser, TOKEN_RBRACKET, "Expect ']' after '[' in array type.");
        }
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

    // A statement terminator is required.
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");

    ASTNode *node = ast_new(parser->arena, AST_VAR_DECL, keyword.line, keyword.column);
    node->as.var_decl.name = name_token.lexeme;
    node->as.var_decl.type = type_node_val;
    node->as.var_decl.initializer = initializer;
    node->as.var_decl.is_const = is_const;
    return node;
}

// Parses the if/else chain without the trailing ';' so that, for an
// `else if` chain, only the outermost statement consumes the terminator.
static ASTNode *if_statement_body(Parser *parser) {
    Token keyword = parser->previous; // 'if'
    consume(parser, TOKEN_LPAREN, "Expect '(' after 'if'.");
    ASTNode *condition = expression(parser);
    consume(parser, TOKEN_RPAREN, "Expect ')' after if condition.");

    ASTNode *then_branch = block(parser);
    ASTNode *else_branch = NULL;

    if (match(parser, TOKEN_ELSE)) {
        if (check(parser, TOKEN_IF)) {
            advance(parser);
            else_branch = if_statement_body(parser);
        } else {
            else_branch = block(parser);
        }
    }

    ASTNode *node = ast_new(parser->arena, AST_IF_STMT, keyword.line, keyword.column);
    node->as.if_stmt.condition = condition;
    node->as.if_stmt.then_branch = then_branch;
    node->as.if_stmt.else_branch = else_branch;
    return node;
}

static ASTNode *if_statement(Parser *parser) {
    ASTNode *node = if_statement_body(parser);
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after if statement.");
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
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after for statement.");

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
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after while statement.");

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
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after case body.");

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
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after switch statement.");

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
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after expression.");

    ASTNode *node = ast_new(parser->arena, AST_EXPR_STMT, start.line, start.column);
    node->as.expr_stmt.expression = expr;
    return node;
}

// func name(type param, ...) { body }
static ASTNode *func_declaration(Parser *parser) {
    Token keyword = parser->previous; // 'func'

    if (!check(parser, TOKEN_IDENTIFIER)) {
        error_current(parser, "Expect function name after 'func'.");
        synchronize(parser);
        return NULL;
    }
    Token name = parser->current;
    advance(parser);

    ASTNode *node = ast_new(parser->arena, AST_FUNC_DECL, keyword.line, keyword.column);
    node->as.func_decl.name = name.lexeme;
    node->as.func_decl.body = NULL;
    ast_node_list_init(parser->arena, &node->as.func_decl.params);

    consume(parser, TOKEN_LPAREN, "Expect '(' after function name.");

    if (!check(parser, TOKEN_RPAREN)) {
        do {
            // Parameter: [type] name  (type may be a keyword type like int,
            // or an identifier-based type like string)
            ASTNode *param = ast_new(parser->arena, AST_VAR_DECL, parser->current.line, parser->current.column);
            param->as.var_decl.is_const = false;
            param->as.var_decl.initializer = NULL;

            if (is_type_token(parser->current.type)) {
                advance(parser);
                param->as.var_decl.type = type_node(parser);
                if (match(parser, TOKEN_LBRACKET)) {
                    consume(parser, TOKEN_RBRACKET, "Expect ']' after '[' in array type.");
                }
            } else if (check(parser, TOKEN_IDENTIFIER) && peek(parser).type == TOKEN_IDENTIFIER) {
                // Identifier-based type followed by a name: `MyType x`
                Token t = parser->current;
                advance(parser);
                param->as.var_decl.type = ast_new(parser->arena, AST_IDENTIFIER, t.line, t.column);
                param->as.var_decl.type->as.identifier.name = t.lexeme;
                if (match(parser, TOKEN_LBRACKET)) {
                    consume(parser, TOKEN_RBRACKET, "Expect ']' after '[' in array type.");
                }
            } else if (check(parser, TOKEN_IDENTIFIER)) {
                // Untyped parameter — values are dynamic, so types are optional.
                param->as.var_decl.type = NULL;
            } else {
                error_current(parser, "Expect parameter type.");
                synchronize(parser);
                return NULL;
            }

            if (check(parser, TOKEN_IDENTIFIER)) {
                Token pname = parser->current;
                advance(parser);
                param->as.var_decl.name = pname.lexeme;
            } else {
                error_current(parser, "Expect parameter name.");
                synchronize(parser);
                return NULL;
            }

            ast_node_list_push(parser->arena, &node->as.func_decl.params, param);
        } while (match(parser, TOKEN_COMMA));
    }

    consume(parser, TOKEN_RPAREN, "Expect ')' after parameters.");
    node->as.func_decl.body = block(parser);
    match(parser, TOKEN_SEMICOLON); // optional trailing ';' e.g. '};'
    return node;
}

// try { ... } catch (name) { ... };
static ASTNode *try_statement(Parser *parser) {
    Token keyword = parser->previous; // 'try'
    ASTNode *try_body = block(parser);
    // A ';' between the try block and `catch` is allowed (and ignored), so
    // both `try { ... } catch (e) { ... };` and `try { ... }; catch (e) { ... };`
    // are accepted.
    match(parser, TOKEN_SEMICOLON);
    consume(parser, TOKEN_CATCH, "Expect 'catch' after try block.");
    consume(parser, TOKEN_LPAREN, "Expect '(' after 'catch'.");
    if (!check(parser, TOKEN_IDENTIFIER)) {
        error_current(parser, "Expect a name for the caught value.");
        synchronize(parser);
        return NULL;
    }
    Token name = parser->current;
    advance(parser);
    consume(parser, TOKEN_RPAREN, "Expect ')' after the catch name.");
    ASTNode *catch_body = block(parser);
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after try/catch statement.");

    ASTNode *node = ast_new(parser->arena, AST_TRY_STMT, keyword.line, keyword.column);
    node->as.try_stmt.try_body = try_body;
    node->as.try_stmt.catch_name = name.lexeme;
    node->as.try_stmt.catch_body = catch_body;
    return node;
}

// throw <expression>;
static ASTNode *throw_statement(Parser *parser) {
    Token keyword = parser->previous; // 'throw'
    ASTNode *value = expression(parser);
    consume(parser, TOKEN_SEMICOLON, "Expect ';' after throw value.");
    ASTNode *node = ast_new(parser->arena, AST_THROW_STMT, keyword.line, keyword.column);
    node->as.throw_stmt.value = value;
    return node;
}

static ASTNode *statement(Parser *parser) {
    if (match(parser, TOKEN_IF)) return if_statement(parser);
    if (match(parser, TOKEN_TRY)) return try_statement(parser);
    if (match(parser, TOKEN_THROW)) return throw_statement(parser);
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
        consume(parser, TOKEN_SEMICOLON, "Expect ';' after block.");
        return node;
    }
    if (match(parser, TOKEN_BREAK)) {
        // Treat as an expression statement referencing the keyword name for codegen simplicity
        consume(parser, TOKEN_SEMICOLON, "Expect ';' after 'break'.");
        ASTNode *node = ast_new(parser->arena, AST_EXPR_STMT, parser->previous.line, parser->previous.column);
        ASTNode *id = ast_new(parser->arena, AST_IDENTIFIER, parser->previous.line, parser->previous.column);
        id->as.identifier.name = sv_from_cstr("break");
        node->as.expr_stmt.expression = id;
        return node;
    }
    if (match(parser, TOKEN_CONTINUE)) {
        consume(parser, TOKEN_SEMICOLON, "Expect ';' after 'continue'.");
        ASTNode *node = ast_new(parser->arena, AST_EXPR_STMT, parser->previous.line, parser->previous.column);
        ASTNode *id = ast_new(parser->arena, AST_IDENTIFIER, parser->previous.line, parser->previous.column);
        id->as.identifier.name = sv_from_cstr("continue");
        node->as.expr_stmt.expression = id;
        return node;
    }
    return expression_statement(parser);
}

static ASTNode *declaration(Parser *parser) {
    if (match(parser, TOKEN_FUNC)) {
        ASTNode *node = func_declaration(parser);
        if (parser->panic_mode) synchronize(parser);
        return node;
    }
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
        if (parser->panic_mode) {
            synchronize(parser);
            // synchronize() stops on '}' without consuming it so enclosing
            // blocks can consume it. At the top level a stray '}' would
            // otherwise loop forever — consume it to make progress.
            if (parser->current.type == TOKEN_RBRACE) {
                advance(parser);
            }
        }
    }

    // EOF is fine as-is
    return program;
}

void parser_print_errors(Parser *parser) {
    (void)parser; // Errors are printed as they occur
}