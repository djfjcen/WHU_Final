#include "toyc/options.h"

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

namespace toyc {
namespace {

CompilerOptions parse(std::initializer_list<const char*> args, DiagnosticEngine& diagnostics) {
    std::vector<char*> argv;
    for (const char* arg : args) {
        argv.push_back(const_cast<char*>(arg));
    }
    return parse_options(static_cast<int>(argv.size()), argv.data(), diagnostics);
}

TEST(Options, ParsesAllFlagsAliasesAndCombinations) {
    DiagnosticEngine diagnostics;
    CompilerOptions options =
        parse({"toyc-compiler", "-dump-tokens", "-lex", "-dump-ast", "-dump-ir", "-dump-asm", "-opt"},
              diagnostics);
    EXPECT_FALSE(diagnostics.has_errors());
    EXPECT_TRUE(options.dump_tokens);
    EXPECT_TRUE(options.dump_ast);
    EXPECT_TRUE(options.dump_ir);
    EXPECT_TRUE(options.opt_mode);
}

TEST(Options, ReportsUnknownArguments) {
    DiagnosticEngine diagnostics;
    CompilerOptions options = parse({"toyc-compiler", "-unknown"}, diagnostics);
    EXPECT_FALSE(options.dump_tokens);
    EXPECT_TRUE(diagnostics.has_errors());
    ASSERT_EQ(1U, diagnostics.diagnostics().size());
    EXPECT_EQ(DiagnosticStage::Driver, diagnostics.diagnostics().front().stage);
    EXPECT_EQ("unknown argument: -unknown", diagnostics.diagnostics().front().message);
}

TEST(Options, PrintsUsageText) {
    std::ostringstream output;
    print_usage(output);
    const std::string text = output.str();
    EXPECT_NE(std::string::npos, text.find("Usage:"));
    EXPECT_NE(std::string::npos, text.find("-dump-tokens"));
    EXPECT_NE(std::string::npos, text.find("-dump-asm"));
    EXPECT_NE(std::string::npos, text.find("-opt"));
}

}  // namespace
}  // namespace toyc
