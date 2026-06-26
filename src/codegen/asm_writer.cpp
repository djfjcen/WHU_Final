#include "toyc/riscv.h"

#include <ostream>

namespace toyc {

void AsmWriter::section(const std::string& name) {
    out_ << "    .section " << name << '\n';
}

void AsmWriter::global(const std::string& label) {
    out_ << "    .globl " << label << '\n';
}

void AsmWriter::label(const std::string& label) {
    out_ << label << ":\n";
}

void AsmWriter::inst(const std::string& op) {
    out_ << "    " << op << '\n';
}

void AsmWriter::inst(const std::string& op, const std::string& a) {
    out_ << "    " << op << ' ' << a << '\n';
}

void AsmWriter::inst(const std::string& op, const std::string& a, const std::string& b) {
    out_ << "    " << op << ' ' << a << ", " << b << '\n';
}

void AsmWriter::inst(const std::string& op, const std::string& a, const std::string& b,
                     const std::string& c) {
    out_ << "    " << op << ' ' << a << ", " << b << ", " << c << '\n';
}

void AsmWriter::comment(const std::string& text) {
    out_ << "    # " << text << '\n';
}

}  // namespace toyc
