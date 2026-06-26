#include "toyc/riscv.h"

#include "toyc/ir.h"

#include <cassert>
#include <string>

namespace toyc {

const char* reg_name(RvReg reg) {
    switch (reg) {
        case RvReg::Zero: return "x0";
        case RvReg::Ra: return "ra";
        case RvReg::Sp: return "sp";
        case RvReg::Gp: return "gp";
        case RvReg::Tp: return "tp";
        case RvReg::T0: return "t0";
        case RvReg::T1: return "t1";
        case RvReg::T2: return "t2";
        case RvReg::S0: return "s0";
        case RvReg::S1: return "s1";
        case RvReg::A0: return "a0";
        case RvReg::A1: return "a1";
        case RvReg::A2: return "a2";
        case RvReg::A3: return "a3";
        case RvReg::A4: return "a4";
        case RvReg::A5: return "a5";
        case RvReg::A6: return "a6";
        case RvReg::A7: return "a7";
        case RvReg::S2: return "s2";
        case RvReg::S3: return "s3";
        case RvReg::S4: return "s4";
        case RvReg::S5: return "s5";
        case RvReg::S6: return "s6";
        case RvReg::S7: return "s7";
        case RvReg::S8: return "s8";
        case RvReg::S9: return "s9";
        case RvReg::S10: return "s10";
        case RvReg::S11: return "s11";
        case RvReg::T3: return "t3";
        case RvReg::T4: return "t4";
        case RvReg::T5: return "t5";
        case RvReg::T6: return "t6";
    }
    return "?";
}

bool fits_i12(int value) {
    return value >= -2048 && value <= 2047;
}

int align_to(int value, int align) {
    assert(align > 0);
    const int remainder = value % align;
    if (remainder == 0) {
        return value;
    }
    return value + align - remainder;
}

std::string function_label(const Function& function) {
    return function.short_name();
}

std::string block_label(const Function& function, const BasicBlock& block) {
    return ".L" + function.short_name() + "_" + block.name();
}

std::string global_label(const GlobalAddr& global) {
    return global.label();
}

}  // namespace toyc
