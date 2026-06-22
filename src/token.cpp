#include "toyc/token.h"

namespace toyc {

bool Token::is_keyword() const {
    switch (type) {
        case TokenType::KW_CONST:
        case TokenType::KW_INT:
        case TokenType::KW_VOID:
        case TokenType::KW_IF:
        case TokenType::KW_ELSE:
        case TokenType::KW_WHILE:
        case TokenType::KW_BREAK:
        case TokenType::KW_CONTINUE:
        case TokenType::KW_RETURN:
            return true;
        default:
            return false;
    }
}

bool Token::is_operand_suffix() const {
    switch (type) {
        case TokenType::IDENT:
        case TokenType::NUMBER:
        case TokenType::RPAREN:
            return true;
        default:
            return false;
    }
}

const char* token_type_name(TokenType type) {
    switch (type) {
        case TokenType::KW_CONST: return "KW_CONST";
        case TokenType::KW_INT: return "KW_INT";
        case TokenType::KW_VOID: return "KW_VOID";
        case TokenType::KW_IF: return "KW_IF";
        case TokenType::KW_ELSE: return "KW_ELSE";
        case TokenType::KW_WHILE: return "KW_WHILE";
        case TokenType::KW_BREAK: return "KW_BREAK";
        case TokenType::KW_CONTINUE: return "KW_CONTINUE";
        case TokenType::KW_RETURN: return "KW_RETURN";
        case TokenType::IDENT: return "IDENT";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::ASSIGN: return "ASSIGN";
        case TokenType::OR: return "OR";
        case TokenType::AND: return "AND";
        case TokenType::LT: return "LT";
        case TokenType::LE: return "LE";
        case TokenType::GT: return "GT";
        case TokenType::GE: return "GE";
        case TokenType::EQ: return "EQ";
        case TokenType::NE: return "NE";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::MUL: return "MUL";
        case TokenType::DIV: return "DIV";
        case TokenType::MOD: return "MOD";
        case TokenType::NOT: return "NOT";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::LBRACE: return "LBRACE";
        case TokenType::RBRACE: return "RBRACE";
        case TokenType::LPAREN: return "LPAREN";
        case TokenType::RPAREN: return "RPAREN";
        case TokenType::COMMA: return "COMMA";
        case TokenType::END_OF_FILE: return "END_OF_FILE";
        case TokenType::INVALID: return "INVALID";
    }
    return "UNKNOWN";
}

std::ostream& operator<<(std::ostream& os, const Token& token) {
    os << token_type_name(token.type);
    if (!token.lexeme.empty()) {
        os << "(\"" << token.lexeme << "\")";
    }
    os << "@" << token.line << ":" << token.column;
    return os;
}

}  // namespace toyc
