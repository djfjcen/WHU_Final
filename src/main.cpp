#include "toyc/lexer.h"

#include <iostream>
#include <string>

namespace {

void print_usage() {
    std::cerr << "Usage:\n"
              << "  toyc-compiler [-lex] [-opt]\n"
              << "    -lex   dump tokens to stderr (debug)\n"
              << "    -opt   enable optimizations (reserved)\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    bool lex_mode = false;
    bool opt_mode = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-lex") {
            lex_mode = true;
        } else if (arg == "-opt") {
            opt_mode = true;
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            print_usage();
            return 1;
        }
    }

    (void)opt_mode;

    toyc::Lexer lexer(std::cin);
    if (lex_mode) {
        while (true) {
            const toyc::Token token = lexer.next_token();
            std::cerr << token << '\n';
            if (token.type == toyc::TokenType::END_OF_FILE ||
                token.type == toyc::TokenType::INVALID) {
                break;
            }
        }
        if (lexer.has_error()) {
            std::cerr << "lex error: " << lexer.error_message() << '\n';
            return 1;
        }
        return 0;
    }

    std::cerr << "parser/codegen not implemented yet; use -lex to test the lexer\n";
    return 1;
}
