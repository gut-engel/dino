#include "lexer.h"
#include "../common.h"
#include <ctype.h>

static char lexer_peek(Lexer *lexer) {
    if (lexer->position >= lexer->length) return '\0';
    return lexer->source[lexer->position];
}

static char lexer_peek_next(Lexer *lexer) {
    if (lexer->position + 1 >= lexer->length) return '\0';
    return lexer->source[lexer->position + 1];
}

static char lexer_advance(Lexer *lexer) {
    if (lexer->position >= lexer->length) return '\0';
    char c = lexer->source[lexer->position++];
    if (c == '\n') {
        lexer->line++;
        lexer->column = 1;
    } else {
        lexer->column++;
    }
    return c;
}

static void lexer_skip_whitespace(Lexer *lexer) {
    while (true) {
        char c = lexer_peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lexer_advance(lexer);
        } else if (c == '/' && lexer_peek_next(lexer) == '/') {
            // Line comment: skip to end of line
            while (lexer_peek(lexer) != '\n' && lexer_peek(lexer) != '\0') {
                lexer_advance(lexer);
            }
        } else if (c == '/' && lexer_peek_next(lexer) == '*') {
            // Block comment: skip to closing '*/'
            lexer_advance(lexer); // consume '/'
            lexer_advance(lexer); // consume '*'
            while (lexer_peek(lexer) != '\0') {
                if (lexer_peek(lexer) == '*' && lexer_peek_next(lexer) == '/') {
                    lexer_advance(lexer);
                    lexer_advance(lexer);
                    break;
                }
                lexer_advance(lexer);
            }
        } else {
            break;
        }
    }
}

static bool lexer_match(Lexer *lexer, char expected) {
    if (lexer_peek(lexer) == expected) {
        lexer_advance(lexer);
        return true;
    }
    return false;
}

static Token lexer_identifier(Lexer *lexer, size_t start_line, size_t start_col) {
    size_t start = lexer->position - 1;
    while (isalnum(lexer_peek(lexer)) || lexer_peek(lexer) == '_') {
        lexer_advance(lexer);
    }
    StringView lexeme = sv_from_parts(lexer->source + start, lexer->position - start);

    // Check for keywords
    if (sv_eq(lexeme, sv_from_cstr("const"))) return make_token(TOKEN_CONST, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("var"))) return make_token(TOKEN_VAR, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("if"))) return make_token(TOKEN_IF, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("else"))) return make_token(TOKEN_ELSE, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("for"))) return make_token(TOKEN_FOR, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("while"))) return make_token(TOKEN_WHILE, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("switch"))) return make_token(TOKEN_SWITCH, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("case"))) return make_token(TOKEN_CASE, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("default"))) return make_token(TOKEN_DEFAULT, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("break"))) return make_token(TOKEN_BREAK, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("continue"))) return make_token(TOKEN_CONTINUE, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("return"))) return make_token(TOKEN_RETURN, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("bool"))) return make_token(TOKEN_BOOL, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("int"))) return make_token(TOKEN_INT, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("float"))) return make_token(TOKEN_FLOAT, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("void"))) return make_token(TOKEN_VOID, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("true"))) return make_token(TOKEN_TRUE, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("false"))) return make_token(TOKEN_FALSE, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("console"))) return make_token(TOKEN_CONSOLE, lexeme, start_line, start_col);
    if (sv_eq(lexeme, sv_from_cstr("func"))) return make_token(TOKEN_FUNC, lexeme, start_line, start_col);

    return make_token(TOKEN_IDENTIFIER, lexeme, start_line, start_col);
}

static Token lexer_number(Lexer *lexer, size_t start_line, size_t start_col) {
    size_t start = lexer->position - 1;
    while (isdigit(lexer_peek(lexer))) {
        lexer_advance(lexer);
    }
    if (lexer_peek(lexer) == '.' && isdigit(lexer_peek_next(lexer))) {
        lexer_advance(lexer); // consume '.'
        while (isdigit(lexer_peek(lexer))) {
            lexer_advance(lexer);
        }
        return make_token(TOKEN_NUMBER, sv_from_parts(lexer->source + start, lexer->position - start), start_line, start_col);
    }
    return make_token(TOKEN_NUMBER, sv_from_parts(lexer->source + start, lexer->position - start), start_line, start_col);
}

static Token lexer_string(Lexer *lexer, size_t start_line, size_t start_col) {
    size_t start = lexer->position - 1;
    bool is_interpolated = false;

    while (lexer_peek(lexer) != '"' && lexer_peek(lexer) != '\0') {
        if (lexer_peek(lexer) == '\\') {
            lexer_advance(lexer); // skip escape
            if (lexer_peek(lexer) != '\0') lexer_advance(lexer);
        } else {
            lexer_advance(lexer);
        }
    }

    if (lexer_peek(lexer) == '\0') {
        return make_error_token("Unterminated string", start_line, start_col);
    }

    lexer_advance(lexer); // consume closing "
    StringView lexeme = sv_from_parts(lexer->source + start, lexer->position - start);

    return make_token(is_interpolated ? TOKEN_INTERPOLATED_STRING : TOKEN_STRING, lexeme, start_line, start_col);
}

// Lexes $"..." (or $"...") where the leading $ has already been consumed.
static Token lexer_interpolated_string(Lexer *lexer, size_t start_line, size_t start_col) {
    size_t start = lexer->position - 2; // include the '$' and opening '"'
    int brace_depth = 0;

    // Keep scanning while still inside ${...} interpolation or not at the
    // closing quote. Stops only when brace_depth returns to 0 AND we see '"'.
    while ((brace_depth > 0 || lexer_peek(lexer) != '"') && lexer_peek(lexer) != '\0') {
        char c = lexer_peek(lexer);
        if (c == '\\') {
            lexer_advance(lexer);
            if (lexer_peek(lexer) != '\0') lexer_advance(lexer);
            continue;
        }
        if (brace_depth > 0 && c == '{') brace_depth++;
        if (brace_depth > 0 && c == '}') brace_depth--;
        if (brace_depth == 0 && c == '{') brace_depth = 1;
        lexer_advance(lexer);
    }

    if (lexer_peek(lexer) == '\0') {
        return make_error_token("Unterminated interpolated string", start_line, start_col);
    }

    lexer_advance(lexer); // consume closing "
    StringView lexeme = sv_from_parts(lexer->source + start, lexer->position - start);
    return make_token(TOKEN_INTERPOLATED_STRING, lexeme, start_line, start_col);
}

Lexer lexer_new(const char *source, size_t length) {
    return (Lexer){.source = source, .length = length, .position = 0, .line = 1, .column = 1};
}

Token lexer_next_token(Lexer *lexer) {
    lexer_skip_whitespace(lexer);

    size_t start_line = lexer->line;
    size_t start_col = lexer->column;

    char c = lexer_advance(lexer);
    if (c == '\0') return make_token(TOKEN_EOF, sv_from_cstr(""), start_line, start_col);

    switch (c) {
        case '(': return make_token(TOKEN_LPAREN, sv_from_cstr("("), start_line, start_col);
        case ')': return make_token(TOKEN_RPAREN, sv_from_cstr(")"), start_line, start_col);
        case '{': return make_token(TOKEN_LBRACE, sv_from_cstr("{"), start_line, start_col);
        case '}': return make_token(TOKEN_RBRACE, sv_from_cstr("}"), start_line, start_col);
        case '[': return make_token(TOKEN_LBRACKET, sv_from_cstr("["), start_line, start_col);
        case ']': return make_token(TOKEN_RBRACKET, sv_from_cstr("]"), start_line, start_col);
        case ';': return make_token(TOKEN_SEMICOLON, sv_from_cstr(";"), start_line, start_col);
        case ',': return make_token(TOKEN_COMMA, sv_from_cstr(","), start_line, start_col);
        case '.': return make_token(TOKEN_DOT, sv_from_cstr("."), start_line, start_col);
        case ':': return make_token(TOKEN_COLON, sv_from_cstr(":"), start_line, start_col);
        case '+':
            if (lexer_match(lexer, '+')) {
                return make_token(TOKEN_PLUS_PLUS, sv_from_cstr("++"), start_line, start_col);
            }
            return make_token(TOKEN_PLUS, sv_from_cstr("+"), start_line, start_col);
        case '-':
            if (lexer_match(lexer, '-')) {
                return make_token(TOKEN_MINUS_MINUS, sv_from_cstr("--"), start_line, start_col);
            }
            if (lexer_match(lexer, '>')) {
                return make_token(TOKEN_ARROW, sv_from_cstr("->"), start_line, start_col);
            }
            return make_token(TOKEN_MINUS, sv_from_cstr("-"), start_line, start_col);
        case '*': return make_token(TOKEN_STAR, sv_from_cstr("*"), start_line, start_col);
        case '/': return make_token(TOKEN_SLASH, sv_from_cstr("/"), start_line, start_col);
        case '%': return make_token(TOKEN_PERCENT, sv_from_cstr("%"), start_line, start_col);
        case '!':
            if (lexer_match(lexer, '=')) {
                return make_token(TOKEN_BANG_EQ, sv_from_cstr("!="), start_line, start_col);
            }
            return make_token(TOKEN_BANG, sv_from_cstr("!"), start_line, start_col);
        case '=':
            if (lexer_match(lexer, '=')) {
                return make_token(TOKEN_EQ_EQ, sv_from_cstr("=="), start_line, start_col);
            }
            return make_token(TOKEN_EQ, sv_from_cstr("="), start_line, start_col);
        case '<':
            if (lexer_match(lexer, '=')) {
                return make_token(TOKEN_LT_EQ, sv_from_cstr("<="), start_line, start_col);
            }
            return make_token(TOKEN_LT, sv_from_cstr("<"), start_line, start_col);
        case '>':
            if (lexer_match(lexer, '=')) {
                return make_token(TOKEN_GT_EQ, sv_from_cstr(">="), start_line, start_col);
            }
            return make_token(TOKEN_GT, sv_from_cstr(">"), start_line, start_col);
        case '&':
            if (lexer_match(lexer, '&')) {
                return make_token(TOKEN_AND_AND, sv_from_cstr("&&"), start_line, start_col);
            }
            return make_error_token("Unexpected character '&'", start_line, start_col);
        case '|':
            if (lexer_match(lexer, '|')) {
                return make_token(TOKEN_OR_OR, sv_from_cstr("||"), start_line, start_col);
            }
            return make_error_token("Unexpected character '|'", start_line, start_col);
        case '$':
            if (lexer_peek(lexer) == '"') {
                lexer_advance(lexer); // consume "
                return lexer_interpolated_string(lexer, start_line, start_col);
            }
            return make_token(TOKEN_DOLLAR, sv_from_cstr("$"), start_line, start_col);
        case '"': return lexer_string(lexer, start_line, start_col);
        default:
            if (isalpha(c) || c == '_') {
                return lexer_identifier(lexer, start_line, start_col);
            }
            if (isdigit(c)) {
                return lexer_number(lexer, start_line, start_col);
            }
            return make_error_token("Unexpected character", start_line, start_col);
    }
}

void lexer_print_token(Token token) {
    printf("Token(%s, \"%.*s\", line=%zu, col=%zu)\n",
           token_type_name(token.type),
           (int)token.lexeme.length, token.lexeme.data,
           token.line, token.column);
}