#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>

namespace toyc {

enum class TokenType {
    // Keywords
    KW_CONST,
    KW_INT,
    KW_VOID,
    KW_IF,
    KW_ELSE,
    KW_WHILE,
    KW_BREAK,
    KW_CONTINUE,
    KW_RETURN,

    // Literals
    IDENT,
    NUMBER,

    // Operators
    ASSIGN,
    OR,
    AND,
    LT,
    LE,
    GT,
    GE,
    EQ,
    NE,
    PLUS,
    MINUS,
    MUL,
    DIV,
    MOD,
    NOT,

    // Delimiters
    SEMICOLON,
    LBRACE,
    RBRACE,
    LPAREN,
    RPAREN,
    COMMA,

    END_OF_FILE,
    INVALID,
};

struct Token {
    TokenType type = TokenType::END_OF_FILE;
    std::string lexeme;
    std::uint32_t line = 1;
    std::uint32_t column = 1;

    bool is_keyword() const;
    bool is_operand_suffix() const;
};

const char* token_type_name(TokenType type);
std::ostream& operator<<(std::ostream& os, const Token& token);

}  // namespace toyc
