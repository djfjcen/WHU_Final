#include "frontend_fixture.h"

#include "toyc/ast_printer.h"
#include "toyc/lexer.h"
#include "toyc/parser.h"

#include <gtest/gtest.h>

#include <sstream>

namespace toyc::test {

LexResult lex_source(const std::string& source) {
    std::istringstream input(source);
    LexResult result;
    Lexer lexer(input, result.diagnostics);
    for (;;) {
        Token token = lexer.next_token();
        result.tokens.push_back(token);
        if (token.type == TokenType::END_OF_FILE || token.type == TokenType::INVALID) {
            break;
        }
    }
    result.lexer_had_error = lexer.has_error();
    result.lexer_error = lexer.error_message();
    return result;
}

ParseResult parse_source(const std::string& source) {
    std::istringstream input(source);
    ParseResult result;
    Lexer lexer(input, result.diagnostics);
    Parser parser(lexer, result.diagnostics);
    result.unit = parser.parse_comp_unit();
    result.parser_had_error = parser.has_error();
    return result;
}

std::string dump_ast_text(const CompUnit& unit) {
    std::ostringstream output;
    dump_ast(output, unit);
    return output.str();
}

const CompUnit::Item& only_item(const CompUnit& unit) {
    EXPECT_EQ(1U, unit.items.size());
    return unit.items.front();
}

const FuncDef& only_function(const CompUnit& unit) {
    const CompUnit::Item& item = only_item(unit);
    EXPECT_EQ(CompUnit::ItemKind::FuncDef, item.kind);
    return item.func_def;
}

const Stmt& only_stmt(const FuncDef& func, std::size_t index) {
    EXPECT_NE(nullptr, func.body);
    EXPECT_LT(index, func.body->body.size());
    return *func.body->body[index];
}

}  // namespace toyc::test
