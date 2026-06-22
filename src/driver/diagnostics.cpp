#include "toyc/diagnostics.h"

#include <iostream>

namespace toyc {

const char* diagnostic_stage_name(DiagnosticStage stage) {
    switch (stage) {
        case DiagnosticStage::Lex:
            return "lex";
        case DiagnosticStage::Parse:
            return "parse";
        case DiagnosticStage::Ast:
            return "ast";
        case DiagnosticStage::Sema:
            return "sema";
        case DiagnosticStage::Driver:
            return "driver";
    }
    return "unknown";
}

const char* diagnostic_level_name(DiagnosticLevel level) {
    switch (level) {
        case DiagnosticLevel::Error:
            return "error";
        case DiagnosticLevel::Warning:
            return "warning";
    }
    return "unknown";
}

void emit_diagnostic(std::ostream& output, const Diagnostic& diagnostic) {
    output << diagnostic_level_name(diagnostic.level) << " [" << diagnostic_stage_name(diagnostic.stage)
           << "] " << diagnostic.loc.line << ':' << diagnostic.loc.column << ": " << diagnostic.message;
}

void DiagnosticEngine::add(DiagnosticLevel level, DiagnosticStage stage, SourceLoc loc, std::string message) {
    diagnostics_.push_back(Diagnostic{level, stage, loc, std::move(message)});
    if (level == DiagnosticLevel::Error) {
        has_errors_ = true;
    }
}

void DiagnosticEngine::error(DiagnosticStage stage, SourceLoc loc, std::string message) {
    add(DiagnosticLevel::Error, stage, loc, std::move(message));
}

void DiagnosticEngine::error(DiagnosticStage stage, const Token& token, std::string message) {
    error(stage, SourceLoc{token.line, token.column}, std::move(message));
}

void DiagnosticEngine::warning(DiagnosticStage stage, SourceLoc loc, std::string message) {
    add(DiagnosticLevel::Warning, stage, loc, std::move(message));
}

bool DiagnosticEngine::has_errors() const {
    return has_errors_;
}

void DiagnosticEngine::emit_all(std::ostream& output) const {
    for (const Diagnostic& diagnostic : diagnostics_) {
        emit_diagnostic(output, diagnostic);
        output << '\n';
    }
}

void DiagnosticEngine::clear() {
    diagnostics_.clear();
    has_errors_ = false;
}

}  // namespace toyc
