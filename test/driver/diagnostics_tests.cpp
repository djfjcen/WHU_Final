#include "toyc/diagnostics.h"

#include <gtest/gtest.h>

#include <sstream>

namespace toyc {
namespace {

TEST(Diagnostics, RecordsErrorsWarningsTokenLocationsAndClears) {
    DiagnosticEngine diagnostics;
    diagnostics.warning(DiagnosticStage::Lex, SourceLoc{1, 2}, "careful");
    EXPECT_FALSE(diagnostics.has_errors());
    diagnostics.error(DiagnosticStage::Parse, Token{TokenType::IDENT, "x", 3, 4}, "bad");
    EXPECT_TRUE(diagnostics.has_errors());

    ASSERT_EQ(2U, diagnostics.diagnostics().size());
    EXPECT_EQ(DiagnosticLevel::Warning, diagnostics.diagnostics()[0].level);
    EXPECT_EQ(DiagnosticStage::Lex, diagnostics.diagnostics()[0].stage);
    EXPECT_EQ(1U, diagnostics.diagnostics()[0].loc.line);
    EXPECT_EQ(DiagnosticLevel::Error, diagnostics.diagnostics()[1].level);
    EXPECT_EQ(3U, diagnostics.diagnostics()[1].loc.line);
    EXPECT_EQ(4U, diagnostics.diagnostics()[1].loc.column);

    diagnostics.clear();
    EXPECT_FALSE(diagnostics.has_errors());
    EXPECT_TRUE(diagnostics.diagnostics().empty());
}

TEST(Diagnostics, FormatsNamesAndEmitsAll) {
    EXPECT_STREQ("lex", diagnostic_stage_name(DiagnosticStage::Lex));
    EXPECT_STREQ("parse", diagnostic_stage_name(DiagnosticStage::Parse));
    EXPECT_STREQ("ast", diagnostic_stage_name(DiagnosticStage::Ast));
    EXPECT_STREQ("sema", diagnostic_stage_name(DiagnosticStage::Sema));
    EXPECT_STREQ("driver", diagnostic_stage_name(DiagnosticStage::Driver));
    EXPECT_STREQ("error", diagnostic_level_name(DiagnosticLevel::Error));
    EXPECT_STREQ("warning", diagnostic_level_name(DiagnosticLevel::Warning));
    EXPECT_STREQ("unknown", diagnostic_stage_name(static_cast<DiagnosticStage>(999)));
    EXPECT_STREQ("unknown", diagnostic_level_name(static_cast<DiagnosticLevel>(999)));

    Diagnostic diagnostic{DiagnosticLevel::Error, DiagnosticStage::Ast, SourceLoc{5, 6}, "missing"};
    std::ostringstream one;
    emit_diagnostic(one, diagnostic);
    EXPECT_EQ("error [ast] 5:6: missing", one.str());

    DiagnosticEngine diagnostics;
    diagnostics.error(DiagnosticStage::Driver, SourceLoc{0, 0}, "unknown");
    diagnostics.warning(DiagnosticStage::Sema, SourceLoc{7, 8}, "unused");
    std::ostringstream all;
    diagnostics.emit_all(all);
    EXPECT_EQ("error [driver] 0:0: unknown\nwarning [sema] 7:8: unused\n", all.str());
}

}  // namespace
}  // namespace toyc
