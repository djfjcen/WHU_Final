#include "toyc/lexer.h"

#include <cctype>

namespace toyc {
namespace {

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_identifier_char(char ch) {
    return is_identifier_start(ch) || std::isdigit(static_cast<unsigned char>(ch));
}

TokenType lookup_keyword(const std::string& word) {
    if (word == "const") return TokenType::KW_CONST;
    if (word == "int") return TokenType::KW_INT;
    if (word == "void") return TokenType::KW_VOID;
    if (word == "if") return TokenType::KW_IF;
    if (word == "else") return TokenType::KW_ELSE;
    if (word == "while") return TokenType::KW_WHILE;
    if (word == "break") return TokenType::KW_BREAK;
    if (word == "continue") return TokenType::KW_CONTINUE;
    if (word == "return") return TokenType::KW_RETURN;
    return TokenType::IDENT;
}

}  // namespace

Lexer::Lexer(std::istream& input, DiagnosticEngine& diagnostics)
    : input_(input), diagnostics_(diagnostics) {
    advance();
}

void Lexer::advance() {
    if (input_.get(peek_char_)) {
        if (peek_char_ == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
    } else {
        eof_ = true;
        peek_char_ = '\0';
    }
}

Token Lexer::make_token(TokenType type, std::string lexeme, std::uint32_t line,
                        std::uint32_t column) {
    return Token{type, std::move(lexeme), line, column};
}

void Lexer::set_error(SourceLoc loc, std::string message) {
    if (!error_message_) {
        error_message_ = message;
        diagnostics_.error(DiagnosticStage::Lex, loc, message);
    }
}

const std::string& Lexer::error_message() const {
    static const std::string kEmpty;
    if (error_message_) {
        return *error_message_;
    }
    return kEmpty;
}

void Lexer::skip_whitespace_and_comments() {
    while (!eof_) {
        if (std::isspace(static_cast<unsigned char>(peek_char_))) {
            advance();
            continue;
        }

        if (peek_char_ == '/' && input_.peek() == '/') {
            advance();
            advance();
            while (!eof_ && peek_char_ != '\n') {
                advance();
            }
            continue;
        }

        if (peek_char_ == '/' && input_.peek() == '*') {
            advance();
            advance();
            bool closed = false;
            while (!eof_) {
                if (peek_char_ == '*' && input_.peek() == '/') {
                    advance();
                    advance();
                    closed = true;
                    break;
                }
                advance();
            }
            if (!closed) {
                set_error(SourceLoc{line_, column_}, "unterminated block comment");
                return;
            }
            continue;
        }

        break;
    }
}

Token Lexer::scan_identifier_or_keyword() {
    const auto start_line = line_;
    const auto start_column = column_;
    std::string lexeme;
    lexeme.push_back(peek_char_);
    advance();

    while (!eof_ && is_identifier_char(peek_char_)) {
        lexeme.push_back(peek_char_);
        advance();
    }

    const auto token_type = lookup_keyword(lexeme);
    return make_token(token_type, std::move(lexeme), start_line, start_column);
}

Token Lexer::scan_number(bool leading_minus) {
    const auto start_line = line_;
    const auto start_column = column_;
    std::string lexeme;

    if (leading_minus) {
        lexeme.push_back('-');
        advance();
    }

    if (eof_) {
        set_error(SourceLoc{start_line, start_column}, "invalid number literal");
        return make_token(TokenType::INVALID, lexeme, start_line, start_column);
    }

    if (peek_char_ == '0') {
        lexeme.push_back('0');
        advance();
    } else if (peek_char_ >= '1' && peek_char_ <= '9') {
        while (!eof_ && std::isdigit(static_cast<unsigned char>(peek_char_))) {
            lexeme.push_back(peek_char_);
            advance();
        }
    } else {
        set_error(SourceLoc{start_line, start_column}, "invalid number literal");
        return make_token(TokenType::INVALID, lexeme, start_line, start_column);
    }

    return make_token(TokenType::NUMBER, std::move(lexeme), start_line, start_column);
}

Token Lexer::next_token() {
    if (error_message_) {
        current_ = make_token(TokenType::INVALID, "", line_, column_);
        return current_;
    }

    skip_whitespace_and_comments();
    if (error_message_) {
        current_ = make_token(TokenType::INVALID, "", line_, column_);
        return current_;
    }

    if (eof_) {
        current_ = make_token(TokenType::END_OF_FILE, "", line_, column_);
        return current_;
    }

    const auto start_line = line_;
    const auto start_column = column_;
    const char ch = peek_char_;

    if (is_identifier_start(ch)) {
        current_ = scan_identifier_or_keyword();
        return current_;
    }

    if (ch == '-' && !current_.is_operand_suffix()) {
        if (!eof_ && (input_.peek() == '0' ||
                      (input_.peek() >= '1' && input_.peek() <= '9'))) {
            current_ = scan_number(true);
            return current_;
        }
    }

    if (std::isdigit(static_cast<unsigned char>(ch))) {
        current_ = scan_number(false);
        return current_;
    }

    advance();

    switch (ch) {
        case '+':
            current_ = make_token(TokenType::PLUS, "+", start_line, start_column);
            return current_;
        case '-':
            current_ = make_token(TokenType::MINUS, "-", start_line, start_column);
            return current_;
        case '*':
            current_ = make_token(TokenType::MUL, "*", start_line, start_column);
            return current_;
        case '%':
            current_ = make_token(TokenType::MOD, "%", start_line, start_column);
            return current_;
        case ';':
            current_ = make_token(TokenType::SEMICOLON, ";", start_line, start_column);
            return current_;
        case '{':
            current_ = make_token(TokenType::LBRACE, "{", start_line, start_column);
            return current_;
        case '}':
            current_ = make_token(TokenType::RBRACE, "}", start_line, start_column);
            return current_;
        case '(':
            current_ = make_token(TokenType::LPAREN, "(", start_line, start_column);
            return current_;
        case ')':
            current_ = make_token(TokenType::RPAREN, ")", start_line, start_column);
            return current_;
        case ',':
            current_ = make_token(TokenType::COMMA, ",", start_line, start_column);
            return current_;
        case '=':
            if (!eof_ && peek_char_ == '=') {
                advance();
                current_ = make_token(TokenType::EQ, "==", start_line, start_column);
            } else {
                current_ = make_token(TokenType::ASSIGN, "=", start_line, start_column);
            }
            return current_;
        case '!':
            if (!eof_ && peek_char_ == '=') {
                advance();
                current_ = make_token(TokenType::NE, "!=", start_line, start_column);
            } else {
                current_ = make_token(TokenType::NOT, "!", start_line, start_column);
            }
            return current_;
        case '<':
            if (!eof_ && peek_char_ == '=') {
                advance();
                current_ = make_token(TokenType::LE, "<=", start_line, start_column);
            } else {
                current_ = make_token(TokenType::LT, "<", start_line, start_column);
            }
            return current_;
        case '>':
            if (!eof_ && peek_char_ == '=') {
                advance();
                current_ = make_token(TokenType::GE, ">=", start_line, start_column);
            } else {
                current_ = make_token(TokenType::GT, ">", start_line, start_column);
            }
            return current_;
        case '/':
            if (!eof_ && (peek_char_ == '/' || peek_char_ == '*')) {
                set_error(SourceLoc{start_line, start_column}, "unexpected comment start inside token");
                current_ = make_token(TokenType::INVALID, "/", start_line, start_column);
            } else {
                current_ = make_token(TokenType::DIV, "/", start_line, start_column);
            }
            return current_;
        case '&':
            if (!eof_ && peek_char_ == '&') {
                advance();
                current_ = make_token(TokenType::AND, "&&", start_line, start_column);
            } else {
                set_error(SourceLoc{start_line, start_column}, "unexpected character '&'");
                current_ = make_token(TokenType::INVALID, "&", start_line, start_column);
            }
            return current_;
        case '|':
            if (!eof_ && peek_char_ == '|') {
                advance();
                current_ = make_token(TokenType::OR, "||", start_line, start_column);
            } else {
                set_error(SourceLoc{start_line, start_column}, "unexpected character '|'");
                current_ = make_token(TokenType::INVALID, "|", start_line, start_column);
            }
            return current_;
        default:
            set_error(SourceLoc{start_line, start_column},
                      std::string("unexpected character '") + ch + "'");
            current_ = make_token(TokenType::INVALID, std::string(1, ch), start_line, start_column);
            return current_;
    }
}

}  // namespace toyc
