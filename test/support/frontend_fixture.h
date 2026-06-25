#pragma once

#include "toyc/ast.h"
#include "toyc/diagnostics.h"
#include "toyc/token.h"

#include <memory>
#include <string>
#include <vector>

namespace toyc::test {

struct LexResult {
    std::vector<Token> tokens;
    DiagnosticEngine diagnostics;
    bool lexer_had_error = false;
    std::string lexer_error;
};

struct ParseResult {
    std::unique_ptr<CompUnit> unit;
    DiagnosticEngine diagnostics;
    bool parser_had_error = false;
};

LexResult lex_source(const std::string& source);
ParseResult parse_source(const std::string& source);
std::string dump_ast_text(const CompUnit& unit);

const CompUnit::Item& only_item(const CompUnit& unit);
const FuncDef& only_function(const CompUnit& unit);
const Stmt& only_stmt(const FuncDef& func, std::size_t index = 0);

}  // namespace toyc::test
