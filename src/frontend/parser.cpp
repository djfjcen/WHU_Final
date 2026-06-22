#include "toyc/parser.h"

#include <cstdlib>
#include <utility>

namespace toyc {
namespace {

SourceLoc loc_from(const Token& token) {
    return SourceLoc{token.line, token.column};
}

}  // namespace

Parser::Parser(Lexer& lexer, DiagnosticEngine& diagnostics)
    : lexer_(lexer), diagnostics_(diagnostics) {
    advance();
}

void Parser::advance() {
    current_ = lexer_.next_token();
}

bool Parser::check(TokenType type) const {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) {
        return false;
    }
    advance();
    return true;
}

Token Parser::expect(TokenType type, const char* message) {
    if (!check(type)) {
        report_error(current_, message);
        return current_;
    }
    Token token = current_;
    advance();
    return token;
}

void Parser::report_error(const Token& token, const char* message) {
    if (!had_error_) {
        had_error_ = true;
        diagnostics_.error(DiagnosticStage::Parse, token, message);
    }
}

SourceLoc Parser::current_loc() const {
    return loc_from(current_);
}

int Parser::parse_number_literal(const std::string& lexeme) {
    return std::stoi(lexeme);
}

std::unique_ptr<CompUnit> Parser::parse_comp_unit() {
    if (lexer_.has_error()) {
        had_error_ = true;
        return nullptr;
    }
    return parse_comp_unit_impl();
}

std::unique_ptr<CompUnit> Parser::parse_comp_unit_impl() {
    auto unit = std::make_unique<CompUnit>();
    while (!check(TokenType::END_OF_FILE) && !check(TokenType::INVALID) && !had_error_) {
        unit->items.push_back(parse_top_level_item());
    }
    if (!had_error_) {
        expect(TokenType::END_OF_FILE, "expected end of file");
    }
    return unit;
}

CompUnit::Item Parser::parse_top_level_item() {
    CompUnit::Item item;
    if (check(TokenType::KW_CONST)) {
        item.kind = CompUnit::ItemKind::GlobalConst;
        item.global_const = parse_global_const_decl();
        return item;
    }
    if (check(TokenType::KW_INT)) {
        advance();
        const Token name = expect(TokenType::IDENT, "expected identifier after 'int'");
        if (check(TokenType::ASSIGN)) {
            item.kind = CompUnit::ItemKind::GlobalVar;
            item.global_var = parse_global_var_decl(name);
            return item;
        }
        if (check(TokenType::LPAREN)) {
            item.kind = CompUnit::ItemKind::FuncDef;
            item.func_def = parse_func_def(FuncReturnType::Int, name);
            return item;
        }
        report_error(current_, "expected '=' or '(' after identifier in global declaration");
        return item;
    }
    if (check(TokenType::KW_VOID)) {
        advance();
        const Token name = expect(TokenType::IDENT, "expected function name after 'void'");
        item.kind = CompUnit::ItemKind::FuncDef;
        item.func_def = parse_func_def(FuncReturnType::Void, name);
        return item;
    }
    report_error(current_, "expected top-level declaration or function definition");
    return item;
}

GlobalConstDecl Parser::parse_global_const_decl() {
    expect(TokenType::KW_CONST, "expected 'const'");
    expect(TokenType::KW_INT, "expected 'int' after 'const'");
    const Token name = expect(TokenType::IDENT, "expected identifier in const declaration");
    expect(TokenType::ASSIGN, "expected '=' in const declaration");
    auto init = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after const declaration");
    return GlobalConstDecl{name.lexeme, std::move(init), loc_from(name)};
}

GlobalVarDecl Parser::parse_global_var_decl(const Token& name_token) {
    expect(TokenType::ASSIGN, "expected '=' in variable declaration");
    auto init = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after variable declaration");
    return GlobalVarDecl{name_token.lexeme, std::move(init), loc_from(name_token)};
}

FuncDef Parser::parse_func_def(FuncReturnType return_type, const Token& name_token) {
    FuncDef def;
    def.return_type = return_type;
    def.name = name_token.lexeme;
    def.loc = loc_from(name_token);
    expect(TokenType::LPAREN, "expected '(' after function name");
    if (!check(TokenType::RPAREN)) {
        def.params = parse_param_list();
    }
    expect(TokenType::RPAREN, "expected ')' after parameter list");
    def.body = parse_block();
    return def;
}

Param Parser::parse_param() {
    expect(TokenType::KW_INT, "expected 'int' in parameter declaration");
    const Token name = expect(TokenType::IDENT, "expected parameter name");
    return Param{name.lexeme, loc_from(name)};
}

std::vector<Param> Parser::parse_param_list() {
    std::vector<Param> params;
    params.push_back(parse_param());
    while (match(TokenType::COMMA)) {
        params.push_back(parse_param());
    }
    return params;
}

std::unique_ptr<BlockStmt> Parser::parse_block() {
    const Token lbrace = expect(TokenType::LBRACE, "expected '{'");
    std::vector<std::unique_ptr<Stmt>> body;
    while (!check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE) && !check(TokenType::INVALID) &&
           !had_error_) {
        body.push_back(parse_stmt());
    }
    expect(TokenType::RBRACE, "expected '}'");
    auto block = std::make_unique<BlockStmt>();
    block->body = std::move(body);
    block->loc = loc_from(lbrace);
    return block;
}

std::unique_ptr<Stmt> Parser::parse_decl_stmt() {
    expect(TokenType::KW_CONST, "expected 'const'");
    expect(TokenType::KW_INT, "expected 'int' after 'const'");
    const Token name = expect(TokenType::IDENT, "expected identifier in const declaration");
    expect(TokenType::ASSIGN, "expected '=' in const declaration");
    auto init = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after const declaration");
    return Stmt::make_const_decl(name.lexeme, std::move(init), loc_from(name));
}

std::unique_ptr<Stmt> Parser::parse_var_decl_stmt(const Token& /*int_token*/, const Token& name_token) {
    expect(TokenType::ASSIGN, "expected '=' in variable declaration");
    auto init = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after variable declaration");
    return Stmt::make_var_decl(name_token.lexeme, std::move(init), loc_from(name_token));
}

std::unique_ptr<Stmt> Parser::parse_if_stmt(const Token& if_token) {
    expect(TokenType::LPAREN, "expected '(' after 'if'");
    auto condition = parse_expr();
    expect(TokenType::RPAREN, "expected ')' after if condition");
    auto then_branch = parse_stmt();
    std::unique_ptr<Stmt> else_branch;
    if (match(TokenType::KW_ELSE)) {
        else_branch = parse_stmt();
    }
    return Stmt::make_if(std::move(condition), std::move(then_branch), std::move(else_branch),
                         loc_from(if_token));
}

std::unique_ptr<Stmt> Parser::parse_while_stmt(const Token& while_token) {
    expect(TokenType::LPAREN, "expected '(' after 'while'");
    auto condition = parse_expr();
    expect(TokenType::RPAREN, "expected ')' after while condition");
    auto body = parse_stmt();
    return Stmt::make_while(std::move(condition), std::move(body), loc_from(while_token));
}

std::unique_ptr<Stmt> Parser::parse_return_stmt(const Token& return_token) {
    std::optional<std::unique_ptr<Expr>> value;
    if (!check(TokenType::SEMICOLON)) {
        value = parse_expr();
    }
    expect(TokenType::SEMICOLON, "expected ';' after return");
    return Stmt::make_return(std::move(value), loc_from(return_token));
}

std::unique_ptr<Stmt> Parser::parse_stmt() {
    if (had_error_) {
        return nullptr;
    }
    if (check(TokenType::LBRACE)) {
        auto block = parse_block();
        return Stmt::make_block(std::move(block->body), block->loc);
    }
    if (match(TokenType::SEMICOLON)) {
        return Stmt::make_empty(current_loc());
    }
    if (check(TokenType::KW_CONST)) {
        return parse_decl_stmt();
    }
    if (check(TokenType::KW_INT)) {
        const Token int_token = current_;
        advance();
        const Token name = expect(TokenType::IDENT, "expected identifier after 'int'");
        return parse_var_decl_stmt(int_token, name);
    }
    if (check(TokenType::KW_IF)) {
        const Token if_token = current_;
        advance();
        return parse_if_stmt(if_token);
    }
    if (check(TokenType::KW_WHILE)) {
        const Token while_token = current_;
        advance();
        return parse_while_stmt(while_token);
    }
    if (check(TokenType::KW_BREAK)) {
        const Token token = current_;
        advance();
        expect(TokenType::SEMICOLON, "expected ';' after 'break'");
        return Stmt::make_break(loc_from(token));
    }
    if (check(TokenType::KW_CONTINUE)) {
        const Token token = current_;
        advance();
        expect(TokenType::SEMICOLON, "expected ';' after 'continue'");
        return Stmt::make_continue(loc_from(token));
    }
    if (check(TokenType::KW_RETURN)) {
        const Token return_token = current_;
        advance();
        return parse_return_stmt(return_token);
    }
    if (check(TokenType::IDENT)) {
        const Token name = current_;
        advance();
        if (match(TokenType::ASSIGN)) {
            auto value = parse_expr();
            expect(TokenType::SEMICOLON, "expected ';' after assignment");
            return Stmt::make_assign(name.lexeme, std::move(value), loc_from(name));
        }

        std::unique_ptr<Expr> expr;
        if (check(TokenType::LPAREN)) {
            expr = Expr::make_call(name.lexeme, parse_call_args(), loc_from(name));
        } else {
            expr = Expr::make_ident(name.lexeme, loc_from(name));
        }
        expr = parse_mul_expr_from_left(std::move(expr));
        expr = parse_add_expr_from_left(std::move(expr));
        expr = parse_rel_expr_from_left(std::move(expr));
        expr = parse_land_expr_from_left(std::move(expr));
        expr = parse_lor_expr_from_left(std::move(expr));
        expect(TokenType::SEMICOLON, "expected ';' after expression");
        return Stmt::make_expr(std::move(expr), loc_from(name));
    }

    const Token start = current_;
    auto expr = parse_expr();
    expect(TokenType::SEMICOLON, "expected ';' after expression");
    return Stmt::make_expr(std::move(expr), loc_from(start));
}

std::vector<std::unique_ptr<Expr>> Parser::parse_call_args() {
    expect(TokenType::LPAREN, "expected '(' in function call");
    std::vector<std::unique_ptr<Expr>> args;
    if (!check(TokenType::RPAREN)) {
        args.push_back(parse_expr());
        while (match(TokenType::COMMA)) {
            args.push_back(parse_expr());
        }
    }
    expect(TokenType::RPAREN, "expected ')' after call arguments");
    return args;
}

std::unique_ptr<Expr> Parser::parse_expr() {
    return parse_lor_expr();
}

std::unique_ptr<Expr> Parser::parse_lor_expr() {
    auto left = parse_land_expr();
    return parse_lor_expr_from_left(std::move(left));
}

std::unique_ptr<Expr> Parser::parse_lor_expr_from_left(std::unique_ptr<Expr> left) {
    while (match(TokenType::OR)) {
        const SourceLoc loc = current_loc();
        auto right = parse_land_expr();
        left = Expr::make_binary(BinaryOp::Or, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_land_expr() {
    auto left = parse_rel_expr();
    return parse_land_expr_from_left(std::move(left));
}

std::unique_ptr<Expr> Parser::parse_land_expr_from_left(std::unique_ptr<Expr> left) {
    while (match(TokenType::AND)) {
        const SourceLoc loc = current_loc();
        auto right = parse_rel_expr();
        left = Expr::make_binary(BinaryOp::And, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_rel_expr() {
    auto left = parse_add_expr();
    return parse_rel_expr_from_left(std::move(left));
}

std::unique_ptr<Expr> Parser::parse_rel_expr_from_left(std::unique_ptr<Expr> left) {
    while (true) {
        BinaryOp op;
        if (match(TokenType::LT)) {
            op = BinaryOp::Lt;
        } else if (match(TokenType::LE)) {
            op = BinaryOp::Le;
        } else if (match(TokenType::GT)) {
            op = BinaryOp::Gt;
        } else if (match(TokenType::GE)) {
            op = BinaryOp::Ge;
        } else if (match(TokenType::EQ)) {
            op = BinaryOp::Eq;
        } else if (match(TokenType::NE)) {
            op = BinaryOp::Ne;
        } else {
            break;
        }
        const SourceLoc loc = current_loc();
        auto right = parse_add_expr();
        left = Expr::make_binary(op, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_add_expr() {
    auto left = parse_mul_expr();
    return parse_add_expr_from_left(std::move(left));
}

std::unique_ptr<Expr> Parser::parse_add_expr_from_left(std::unique_ptr<Expr> left) {
    while (true) {
        BinaryOp op;
        if (match(TokenType::PLUS)) {
            op = BinaryOp::Add;
        } else if (match(TokenType::MINUS)) {
            op = BinaryOp::Sub;
        } else {
            break;
        }
        const SourceLoc loc = current_loc();
        auto right = parse_mul_expr();
        left = Expr::make_binary(op, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_mul_expr() {
    auto left = parse_unary_expr();
    return parse_mul_expr_from_left(std::move(left));
}

std::unique_ptr<Expr> Parser::parse_mul_expr_from_left(std::unique_ptr<Expr> left) {
    while (true) {
        BinaryOp op;
        if (match(TokenType::MUL)) {
            op = BinaryOp::Mul;
        } else if (match(TokenType::DIV)) {
            op = BinaryOp::Div;
        } else if (match(TokenType::MOD)) {
            op = BinaryOp::Mod;
        } else {
            break;
        }
        const SourceLoc loc = current_loc();
        auto right = parse_unary_expr();
        left = Expr::make_binary(op, std::move(left), std::move(right), loc);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_unary_expr() {
    if (match(TokenType::PLUS)) {
        const SourceLoc loc = current_loc();
        auto operand = parse_unary_expr();
        return Expr::make_unary(UnaryOp::Plus, std::move(operand), loc);
    }
    if (match(TokenType::MINUS)) {
        const SourceLoc loc = current_loc();
        auto operand = parse_unary_expr();
        return Expr::make_unary(UnaryOp::Minus, std::move(operand), loc);
    }
    if (match(TokenType::NOT)) {
        const SourceLoc loc = current_loc();
        auto operand = parse_unary_expr();
        return Expr::make_unary(UnaryOp::Not, std::move(operand), loc);
    }
    return parse_primary_expr();
}

std::unique_ptr<Expr> Parser::parse_primary_expr() {
    if (check(TokenType::NUMBER)) {
        const Token number = current_;
        advance();
        return Expr::make_int_literal(parse_number_literal(number.lexeme), number.lexeme, loc_from(number));
    }
    if (match(TokenType::LPAREN)) {
        const SourceLoc loc = current_loc();
        auto expr = parse_expr();
        expect(TokenType::RPAREN, "expected ')' after expression");
        return expr;
    }
    if (check(TokenType::IDENT)) {
        const Token name = current_;
        advance();
        if (check(TokenType::LPAREN)) {
            return Expr::make_call(name.lexeme, parse_call_args(), loc_from(name));
        }
        return Expr::make_ident(name.lexeme, loc_from(name));
    }
    report_error(current_, "expected expression");
    return Expr::make_int_literal(0, "0", current_loc());
}

}  // namespace toyc
