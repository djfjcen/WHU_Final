#pragma once

#include <iosfwd>
#include <string>

namespace toyc {

class BasicBlock;
class Function;
class GlobalAddr;

enum class RvReg {
    Zero, Ra, Sp, Gp, Tp,
    T0, T1, T2,
    S0, S1,
    A0, A1, A2, A3, A4, A5, A6, A7,
    S2, S3, S4, S5, S6, S7, S8, S9, S10, S11,
    T3, T4, T5, T6,
};

const char* reg_name(RvReg reg);
bool fits_i12(int value);
int align_to(int value, int align);

std::string function_label(const Function& function);
std::string block_label(const Function& function, const BasicBlock& block);
std::string global_label(const GlobalAddr& global);

class AsmWriter {
public:
    explicit AsmWriter(std::ostream& out) : out_(out) {}

    void section(const std::string& name);
    void global(const std::string& label);
    void label(const std::string& label);
    void inst(const std::string& op);
    void inst(const std::string& op, const std::string& a);
    void inst(const std::string& op, const std::string& a, const std::string& b);
    void inst(const std::string& op, const std::string& a, const std::string& b,
              const std::string& c);
    void comment(const std::string& text);

private:
    std::ostream& out_;
};

}  // namespace toyc
