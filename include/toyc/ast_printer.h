#pragma once

#include "toyc/ast.h"

#include <ostream>

namespace toyc {

class ASTPrinter {
public:
    explicit ASTPrinter(std::ostream& output);

    void print(const CompUnit& unit);
    void print(const Stmt& stmt);
    void print(const Expr& expr);

private:
    std::ostream& out_;
    int indent_ = 0;

    void print_indent();
    void print_loc(const SourceLoc& loc);
    void print_params(const std::vector<Param>& params);
    void print_block_body(const BlockStmt& block);
};

void dump_ast(std::ostream& output, const CompUnit& unit);

}  // namespace toyc
