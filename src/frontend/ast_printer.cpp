#include "toyc/ast_printer.h"

namespace toyc {

ASTPrinter::ASTPrinter(std::ostream& output) : out_(output) {}

void ASTPrinter::print_indent() {
    for (int i = 0; i < indent_; ++i) {
        out_ << "  ";
    }
}

void ASTPrinter::print_loc(const SourceLoc& loc) {
    out_ << " @" << loc.line << ":" << loc.column;
}

void ASTPrinter::print(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::IntLiteral:
            print_indent();
            out_ << "IntLiteral(" << expr.int_literal.lexeme << ")";
            print_loc(expr.int_literal.loc);
            break;
        case Expr::Kind::Ident:
            print_indent();
            out_ << "Ident(" << expr.ident.name << ")";
            print_loc(expr.ident.loc);
            break;
        case Expr::Kind::Binary:
            print_indent();
            out_ << "Binary(" << binary_op_name(expr.binary.op) << ")";
            print_loc(expr.binary.loc);
            out_ << '\n';
            ++indent_;
            print(*expr.binary.lhs);
            out_ << '\n';
            print(*expr.binary.rhs);
            --indent_;
            return;
        case Expr::Kind::Unary:
            print_indent();
            out_ << "Unary(" << unary_op_name(expr.unary.op) << ")";
            print_loc(expr.unary.loc);
            out_ << '\n';
            ++indent_;
            print(*expr.unary.operand);
            --indent_;
            return;
        case Expr::Kind::Call:
            print_indent();
            out_ << "Call(" << expr.call.callee << ")";
            print_loc(expr.call.loc);
            out_ << '\n';
            ++indent_;
            for (const auto& arg : expr.call.args) {
                print(*arg);
                out_ << '\n';
            }
            --indent_;
            return;
    }
    out_ << '\n';
}

void ASTPrinter::print(const Stmt& stmt) {
    switch (stmt.kind) {
        case Stmt::Kind::Block:
            print_indent();
            out_ << "Block";
            print_loc(stmt.block.loc);
            out_ << '\n';
            print_block_body(stmt.block);
            return;
        case Stmt::Kind::Empty:
            print_indent();
            out_ << "EmptyStmt";
            print_loc(stmt.empty.loc);
            break;
        case Stmt::Kind::Expr:
            print_indent();
            out_ << "ExprStmt";
            print_loc(stmt.expr.loc);
            out_ << '\n';
            ++indent_;
            print(*stmt.expr.expr);
            --indent_;
            return;
        case Stmt::Kind::Assign:
            print_indent();
            out_ << "Assign(" << stmt.assign.name << ")";
            print_loc(stmt.assign.loc);
            out_ << '\n';
            ++indent_;
            print(*stmt.assign.value);
            --indent_;
            return;
        case Stmt::Kind::ConstDecl:
            print_indent();
            out_ << "ConstDecl(" << stmt.const_decl.name << ")";
            print_loc(stmt.const_decl.loc);
            out_ << '\n';
            ++indent_;
            print(*stmt.const_decl.init);
            --indent_;
            return;
        case Stmt::Kind::VarDecl:
            print_indent();
            out_ << "VarDecl(" << stmt.var_decl.name << ")";
            print_loc(stmt.var_decl.loc);
            out_ << '\n';
            ++indent_;
            print(*stmt.var_decl.init);
            --indent_;
            return;
        case Stmt::Kind::If:
            print_indent();
            out_ << "If";
            print_loc(stmt.if_stmt.loc);
            out_ << '\n';
            ++indent_;
            print_indent();
            out_ << "Cond:\n";
            ++indent_;
            print(*stmt.if_stmt.condition);
            out_ << '\n';
            --indent_;
            print_indent();
            out_ << "Then:\n";
            ++indent_;
            print(*stmt.if_stmt.then_branch);
            out_ << '\n';
            --indent_;
            if (stmt.if_stmt.else_branch) {
                print_indent();
                out_ << "Else:\n";
                ++indent_;
                print(*stmt.if_stmt.else_branch);
                out_ << '\n';
                --indent_;
            }
            --indent_;
            return;
        case Stmt::Kind::While:
            print_indent();
            out_ << "While";
            print_loc(stmt.while_stmt.loc);
            out_ << '\n';
            ++indent_;
            print_indent();
            out_ << "Cond:\n";
            ++indent_;
            print(*stmt.while_stmt.condition);
            out_ << '\n';
            --indent_;
            print_indent();
            out_ << "Body:\n";
            ++indent_;
            print(*stmt.while_stmt.body);
            out_ << '\n';
            --indent_;
            --indent_;
            return;
        case Stmt::Kind::Break:
            print_indent();
            out_ << "Break";
            print_loc(stmt.break_stmt.loc);
            break;
        case Stmt::Kind::Continue:
            print_indent();
            out_ << "Continue";
            print_loc(stmt.continue_stmt.loc);
            break;
        case Stmt::Kind::Return:
            print_indent();
            out_ << "Return";
            print_loc(stmt.return_stmt.loc);
            if (stmt.return_stmt.value) {
                out_ << '\n';
                ++indent_;
                print(**stmt.return_stmt.value);
                --indent_;
            }
            return;
    }
    out_ << '\n';
}

void ASTPrinter::print_params(const std::vector<Param>& params) {
    out_ << '(';
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i > 0) {
            out_ << ", ";
        }
        out_ << "int " << params[i].name;
    }
    out_ << ')';
}

void ASTPrinter::print_block_body(const BlockStmt& block) {
    ++indent_;
    for (const auto& stmt : block.body) {
        print(*stmt);
        out_ << '\n';
    }
    --indent_;
}

void ASTPrinter::print(const CompUnit& unit) {
    out_ << "CompUnit\n";
    ++indent_;
    for (const auto& item : unit.items) {
        switch (item.kind) {
            case CompUnit::ItemKind::GlobalConst:
                print_indent();
                out_ << "GlobalConst(" << item.global_const.name << ")";
                print_loc(item.global_const.loc);
                out_ << '\n';
                ++indent_;
                print(*item.global_const.init);
                out_ << '\n';
                --indent_;
                break;
            case CompUnit::ItemKind::GlobalVar:
                print_indent();
                out_ << "GlobalVar(" << item.global_var.name << ")";
                print_loc(item.global_var.loc);
                out_ << '\n';
                ++indent_;
                print(*item.global_var.init);
                out_ << '\n';
                --indent_;
                break;
            case CompUnit::ItemKind::FuncDef:
                print_indent();
                out_ << "FuncDef " << func_return_type_name(item.func_def.return_type) << ' '
                     << item.func_def.name;
                print_params(item.func_def.params);
                print_loc(item.func_def.loc);
                out_ << '\n';
                ++indent_;
                print_block_body(*item.func_def.body);
                --indent_;
                break;
        }
    }
    --indent_;
}

void dump_ast(std::ostream& output, const CompUnit& unit) {
    ASTPrinter printer(output);
    printer.print(unit);
}

}  // namespace toyc
