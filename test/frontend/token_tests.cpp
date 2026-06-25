#include "toyc/token.h"

#include <gtest/gtest.h>

#include <sstream>

namespace toyc {
namespace {

TEST(Token, NamesKeywordsAndOperandSuffixes) {
    const std::pair<TokenType, const char*> names[] = {
        {TokenType::KW_CONST, "KW_CONST"},       {TokenType::KW_INT, "KW_INT"},
        {TokenType::KW_VOID, "KW_VOID"},         {TokenType::KW_IF, "KW_IF"},
        {TokenType::KW_ELSE, "KW_ELSE"},         {TokenType::KW_WHILE, "KW_WHILE"},
        {TokenType::KW_BREAK, "KW_BREAK"},       {TokenType::KW_CONTINUE, "KW_CONTINUE"},
        {TokenType::KW_RETURN, "KW_RETURN"},     {TokenType::IDENT, "IDENT"},
        {TokenType::NUMBER, "NUMBER"},           {TokenType::ASSIGN, "ASSIGN"},
        {TokenType::OR, "OR"},                   {TokenType::AND, "AND"},
        {TokenType::LT, "LT"},                   {TokenType::LE, "LE"},
        {TokenType::GT, "GT"},                   {TokenType::GE, "GE"},
        {TokenType::EQ, "EQ"},                   {TokenType::NE, "NE"},
        {TokenType::PLUS, "PLUS"},               {TokenType::MINUS, "MINUS"},
        {TokenType::MUL, "MUL"},                 {TokenType::DIV, "DIV"},
        {TokenType::MOD, "MOD"},                 {TokenType::NOT, "NOT"},
        {TokenType::SEMICOLON, "SEMICOLON"},     {TokenType::LBRACE, "LBRACE"},
        {TokenType::RBRACE, "RBRACE"},           {TokenType::LPAREN, "LPAREN"},
        {TokenType::RPAREN, "RPAREN"},           {TokenType::COMMA, "COMMA"},
        {TokenType::END_OF_FILE, "END_OF_FILE"}, {TokenType::INVALID, "INVALID"},
    };
    for (const auto& [type, name] : names) {
        EXPECT_STREQ(name, token_type_name(type));
    }
    EXPECT_STREQ("UNKNOWN", token_type_name(static_cast<TokenType>(999)));

    const TokenType keywords[] = {
        TokenType::KW_CONST, TokenType::KW_INT,      TokenType::KW_VOID,
        TokenType::KW_IF,    TokenType::KW_ELSE,     TokenType::KW_WHILE,
        TokenType::KW_BREAK, TokenType::KW_CONTINUE, TokenType::KW_RETURN,
    };
    for (TokenType type : keywords) {
        EXPECT_TRUE((Token{type, "", 1, 1}.is_keyword())) << token_type_name(type);
    }

    EXPECT_FALSE((Token{TokenType::IDENT, "x", 1, 1}.is_keyword()));
    EXPECT_TRUE((Token{TokenType::IDENT, "x", 1, 1}.is_operand_suffix()));
    EXPECT_TRUE((Token{TokenType::NUMBER, "1", 1, 1}.is_operand_suffix()));
    EXPECT_TRUE((Token{TokenType::RPAREN, ")", 1, 1}.is_operand_suffix()));
    EXPECT_FALSE((Token{TokenType::LPAREN, "(", 1, 1}.is_operand_suffix()));

}

TEST(Token, StreamsWithAndWithoutLexeme) {
    std::ostringstream with_lexeme;
    with_lexeme << Token{TokenType::IDENT, "value", 3, 9};
    EXPECT_EQ("IDENT(\"value\")@3:9", with_lexeme.str());

    std::ostringstream without_lexeme;
    without_lexeme << Token{TokenType::END_OF_FILE, "", 4, 1};
    EXPECT_EQ("END_OF_FILE@4:1", without_lexeme.str());
}

}  // namespace
}  // namespace toyc
