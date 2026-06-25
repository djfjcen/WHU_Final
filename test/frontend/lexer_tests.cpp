#include "frontend_fixture.h"

#include "toyc/lexer.h"

#include <gtest/gtest.h>

#include <sstream>

namespace toyc {
namespace {

std::vector<TokenType> token_types(const std::vector<Token>& tokens) {
    std::vector<TokenType> types;
    for (const Token& token : tokens) {
        types.push_back(token.type);
    }
    return types;
}

TEST(Lexer, ScansKeywordsIdentifiersAndDecimalIntegers) {
    const auto result = test::lex_source(
        "const int void if else while break continue return name _x12 0 42 =-7 =-0 a-8");
    EXPECT_FALSE(result.lexer_had_error);
    EXPECT_EQ((std::vector<TokenType>{
                  TokenType::KW_CONST, TokenType::KW_INT, TokenType::KW_VOID,
                  TokenType::KW_IF, TokenType::KW_ELSE, TokenType::KW_WHILE,
                  TokenType::KW_BREAK, TokenType::KW_CONTINUE, TokenType::KW_RETURN,
                  TokenType::IDENT, TokenType::IDENT, TokenType::NUMBER, TokenType::NUMBER,
                  TokenType::ASSIGN, TokenType::NUMBER, TokenType::ASSIGN, TokenType::NUMBER,
                  TokenType::IDENT, TokenType::MINUS, TokenType::NUMBER, TokenType::END_OF_FILE,
              }),
              token_types(result.tokens));
    EXPECT_EQ("-7", result.tokens[14].lexeme);
    EXPECT_EQ("-0", result.tokens[16].lexeme);
}

TEST(Lexer, ScansOperatorsAndDelimiters) {
    const auto result = test::lex_source("= || && < <= > >= == != + - * / % ! ; { } ( ) ,");
    EXPECT_FALSE(result.lexer_had_error);
    EXPECT_EQ((std::vector<TokenType>{
                  TokenType::ASSIGN, TokenType::OR, TokenType::AND, TokenType::LT,
                  TokenType::LE, TokenType::GT, TokenType::GE, TokenType::EQ,
                  TokenType::NE, TokenType::PLUS, TokenType::MINUS, TokenType::MUL,
                  TokenType::DIV, TokenType::MOD, TokenType::NOT, TokenType::SEMICOLON,
                  TokenType::LBRACE, TokenType::RBRACE, TokenType::LPAREN,
                  TokenType::RPAREN, TokenType::COMMA, TokenType::END_OF_FILE,
              }),
              token_types(result.tokens));
}

TEST(Lexer, SkipsWhitespaceLineCommentsAndBlockCommentsWithLocations) {
    const auto result = test::lex_source(" \t// comment\n  int/* block\ncomment */x");
    ASSERT_FALSE(result.lexer_had_error);
    ASSERT_GE(result.tokens.size(), 3U);
    EXPECT_EQ(TokenType::KW_INT, result.tokens[0].type);
    EXPECT_EQ(2U, result.tokens[0].line);
    EXPECT_EQ(4U, result.tokens[0].column);
    EXPECT_EQ(TokenType::IDENT, result.tokens[1].type);
    EXPECT_EQ(3U, result.tokens[1].line);
    EXPECT_EQ(12U, result.tokens[1].column);

    auto eof_comment = test::lex_source("// comment at eof");
    EXPECT_EQ((std::vector<TokenType>{TokenType::END_OF_FILE}), token_types(eof_comment.tokens));

    auto star_not_close = test::lex_source("/**x*/int");
    ASSERT_FALSE(star_not_close.lexer_had_error);
    EXPECT_EQ(TokenType::KW_INT, star_not_close.tokens.front().type);
}

TEST(Lexer, ReportsIllegalCharactersAndStopsAtStoredError) {
    auto amp = test::lex_source("&");
    ASSERT_EQ(TokenType::INVALID, amp.tokens.front().type);
    EXPECT_TRUE(amp.lexer_had_error);
    EXPECT_EQ("unexpected character '&'", amp.lexer_error);
    ASSERT_EQ(1U, amp.diagnostics.diagnostics().size());
    EXPECT_EQ("unexpected character '&'", test::lex_source("&x").lexer_error);

    auto pipe = test::lex_source("|");
    ASSERT_EQ(TokenType::INVALID, pipe.tokens.front().type);
    EXPECT_EQ("unexpected character '|'", pipe.lexer_error);
    EXPECT_EQ("unexpected character '|'", test::lex_source("|x").lexer_error);

    auto unknown = test::lex_source("@");
    ASSERT_EQ(TokenType::INVALID, unknown.tokens.front().type);
    EXPECT_EQ("unexpected character '@'", unknown.lexer_error);
}

TEST(Lexer, ReportsUnclosedBlockCommentInvalidNumberAndCurrentToken) {
    auto comment = test::lex_source("/* open");
    ASSERT_EQ(TokenType::INVALID, comment.tokens.front().type);
    EXPECT_TRUE(comment.lexer_had_error);
    EXPECT_EQ("unterminated block comment", comment.lexer_error);

    DiagnosticEngine error_diagnostics;
    std::istringstream error_input("&");
    Lexer error_lexer(error_input, error_diagnostics);
    EXPECT_EQ(TokenType::INVALID, error_lexer.next_token().type);
    EXPECT_EQ(TokenType::INVALID, error_lexer.next_token().type);

    auto minus = test::lex_source("- -x");
    ASSERT_FALSE(minus.lexer_had_error);
    EXPECT_EQ(TokenType::MINUS, minus.tokens.front().type);
    EXPECT_EQ(TokenType::MINUS, minus.tokens[1].type);

    DiagnosticEngine diagnostics;
    std::istringstream input("x 1");
    Lexer lexer(input, diagnostics);
    EXPECT_EQ(TokenType::END_OF_FILE, lexer.current_token().type);
    Token ident = lexer.next_token();
    EXPECT_EQ(TokenType::IDENT, ident.type);
    EXPECT_EQ("x", lexer.current_token().lexeme);
    EXPECT_FALSE(lexer.has_error());
    EXPECT_TRUE(lexer.error_message().empty());
}

}  // namespace
}  // namespace toyc
