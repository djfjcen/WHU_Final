#pragma once

#include "toyc/ast.h"
#include "toyc/token.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace toyc {

enum class DiagnosticStage { Lex, Parse, Ast, Sema, Driver };

enum class DiagnosticLevel { Error, Warning };

struct Diagnostic {
    DiagnosticLevel level = DiagnosticLevel::Error;
    DiagnosticStage stage = DiagnosticStage::Driver;
    SourceLoc loc;
    std::string message;
};

class DiagnosticEngine {
public:
    void error(DiagnosticStage stage, SourceLoc loc, std::string message);
    void error(DiagnosticStage stage, const Token& token, std::string message);
    void warning(DiagnosticStage stage, SourceLoc loc, std::string message);

    bool has_errors() const;
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    void emit_all(std::ostream& output) const;
    void clear();

private:
    std::vector<Diagnostic> diagnostics_;
    bool has_errors_ = false;

    void add(DiagnosticLevel level, DiagnosticStage stage, SourceLoc loc, std::string message);
};

const char* diagnostic_stage_name(DiagnosticStage stage);
const char* diagnostic_level_name(DiagnosticLevel level);
void emit_diagnostic(std::ostream& output, const Diagnostic& diagnostic);

}  // namespace toyc
