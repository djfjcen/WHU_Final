#include "toyc/ir_printer.h"
#include "toyc/ir.h"

#include <ostream>

namespace toyc {

namespace {

std::string result_prefix(const Instruction& inst) {
    return inst.has_result() ? inst.name() + " = " : "";
}

void print_operand(Value* v, std::ostream& out) {
    if (v->value_kind() == ValueKind::BasicBlock) {
        out << "label " << v->name();
    } else {
        out << v->name();
    }
}

void print_instruction(const Instruction& inst, std::ostream& out) {
    out << "  " << result_prefix(inst);
    switch (inst.opcode()) {
        case Opcode::Alloca:
            out << "alloca i32";
            break;
        case Opcode::Load:
            out << "load ";
            print_operand(inst.operand(0), out);
            break;
        case Opcode::Store:
            out << "store ";
            print_operand(inst.operand(0), out);
            out << ", ";
            print_operand(inst.operand(1), out);
            break;
        case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
        case Opcode::Sdiv: case Opcode::Srem: case Opcode::And:
            out << opcode_name(inst.opcode()) << " ";
            print_operand(inst.operand(0), out);
            out << ", ";
            print_operand(inst.operand(1), out);
            break;
        case Opcode::Neg:
            out << "neg ";
            print_operand(inst.operand(0), out);
            break;
        case Opcode::ICmpEq: case Opcode::ICmpNe: case Opcode::ICmpSlt:
        case Opcode::ICmpSgt: case Opcode::ICmpSle: case Opcode::ICmpSge:
            out << opcode_name(inst.opcode()) << " ";
            print_operand(inst.operand(0), out);
            out << ", ";
            print_operand(inst.operand(1), out);
            break;
        case Opcode::Br:
            out << "br ";
            print_operand(inst.operand(0), out);
            break;
        case Opcode::CondBr:
            out << "cond_br ";
            print_operand(inst.operand(0), out);
            out << ", ";
            print_operand(inst.operand(1), out);
            out << ", ";
            print_operand(inst.operand(2), out);
            break;
        case Opcode::Ret:
            out << "ret";
            if (inst.num_operands() == 1) {
                out << " ";
                print_operand(inst.operand(0), out);
            }
            break;
        case Opcode::Call: {
            const CallInst& c = static_cast<const CallInst&>(inst);
            out << "call @" << c.callee_name();
            for (unsigned i = 0; i < c.num_operands(); ++i) {
                out << ", ";
                print_operand(c.operand(i), out);
            }
            break;
        }
        case Opcode::Phi: {
            const PhiInst& p = static_cast<const PhiInst&>(inst);
            out << "phi ";
            for (unsigned i = 0; i < p.num_operands(); ++i) {
                if (i) {
                    out << ", ";
                }
                out << "[";
                print_operand(p.operand(i), out);
                out << ", " << p.incoming_blocks()[i]->name() << "]";
            }
            break;
        }
        case Opcode::Shl: case Opcode::Shr: {
            const Instruction& base = inst;
            unsigned amount = (inst.opcode() == Opcode::Shl)
                                  ? static_cast<const ShlInst&>(base).amount()
                                  : static_cast<const ShrInst&>(base).amount();
            out << opcode_name(inst.opcode()) << " ";
            print_operand(inst.operand(0), out);
            out << ", " << amount;
            break;
        }
    }
    out << "\n";
}

}  // namespace

void print_function(const Function& fn, std::ostream& out) {
    out << "define " << (fn.ret_type() == FuncRet::Int ? "i32" : "void") << " " << fn.name() << "(";
    for (unsigned i = 0; i < fn.params().size(); ++i) {
        if (i) {
            out << ", ";
        }
        out << "i32 " << fn.params()[i]->name();
    }
    out << ") {\n";
    for (const std::unique_ptr<BasicBlock>& bb : fn.blocks()) {
        out << bb->name() << ":\n";
        for (const std::unique_ptr<Instruction>& inst : bb->insts()) {
            print_instruction(*inst, out);
        }
    }
    out << "}\n";
}

void print_module(const Module& module, std::ostream& out) {
    for (const std::unique_ptr<GlobalVar>& g : module.globals()) {
        out << g->addr->name() << " = " << (g->is_const ? "const" : "global") << " i32 "
            << g->init->value() << "\n";
    }
    if (!module.globals().empty()) {
        out << "\n";
    }
    for (const std::unique_ptr<Function>& fn : module.functions()) {
        print_function(*fn, out);
    }
}

}  // namespace toyc
