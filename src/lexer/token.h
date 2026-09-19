#ifndef TOKEN_H
#define TOKEN_H

#include "../common.h"

typedef enum {
    TOKEN_EOF,
    TOKEN_ERROR,

    // Literals
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_INTERPOLATED_STRING,

    // Keywords
    TOKEN_CONST,
    TOKEN_VAR,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_FOR,
    TOKEN_WHILE,
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_DEFAULT,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_RETURN,
    TOKEN_BOOL,
    TOKEN_INT,
    TOKEN_FLOAT,
    TOKEN_VOID,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_NULL,
    TOKEN_CONSOLE,
    TOKEN_FUNC,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_THROW,

    // Operators
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_PLUS_PLUS,
    TOKEN_MINUS_MINUS,
    TOKEN_EQ,
    TOKEN_EQ_EQ,
    TOKEN_BANG_EQ,
    TOKEN_LT,
    TOKEN_LT_EQ,
    TOKEN_GT,
    TOKEN_GT_EQ,
    TOKEN_AND_AND,
    TOKEN_OR_OR,
    TOKEN_BANG,
    TOKEN_DOLLAR,

    // Punctuation
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_SEMICOLON,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_COLON,
    TOKEN_ARROW,
} TokenType;

typedef struct {
    TokenType type;
    StringView lexeme;
    size_t line;
    size_t column;
} Token;

const char *token_type_name(TokenType type);
Token make_token(TokenType type, StringView lexeme, size_t line, size_t column);
Token make_error_token(const char *message, size_t line, size_t column);

#endif