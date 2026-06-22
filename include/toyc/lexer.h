#pragma once

#include "toyc/diagnostics.h"
#include "toyc/token.h"

#include <istream>
#include <optional>
#include <string>

namespace toyc {

class Lexer {
public:
    Lexer(std::istream& input, DiagnosticEngine& diagnostics);

    Token next_token();
    const Token& current_token() const { return current_; }
    bool has_error() const { return error_message_.has_value(); }
    const std::string& error_message() const;

private:
    std::istream& input_;
    DiagnosticEngine& diagnostics_;
    Token current_{TokenType::END_OF_FILE, "", 1, 1};
    std::optional<std::string> error_message_;

    char peek_char_ = '\0';
    bool eof_ = false;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 0;

    void advance();
    void skip_whitespace_and_comments();
    Token make_token(TokenType type, std::string lexeme, std::uint32_t line, std::uint32_t column);
    Token scan_identifier_or_keyword();
    Token scan_number(bool leading_minus);
    void set_error(SourceLoc loc, std::string message);
};

}  // namespace toyc
