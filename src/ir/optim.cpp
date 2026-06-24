#include "toyc/optim.h"

#include "toyc/ir.h"

#include <optional>
#include <unordered_set>

namespace toyc {

namespace {

// Erase every instruction in `dead`: first detach it from its operands' use
// lists (so no dangling User* survives), then remove it from its block.
void erase_dead(Function& fn, const std::unordered_set<Instruction*>& dead) {
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        for (const std::unique_ptr<Instruction>& inst : owner->insts()) {
            if (!dead.count(inst.get())) continue;
            for (unsigned k = 0; k < inst->num_operands(); ++k) {
                if (Value* op = inst->operand(k)) op->remove_use(inst.get());
            }
        }
    }
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        std::list<std::unique_ptr<Instruction>>& insts = owner->insts();
        for (auto it = insts.begin(); it != insts.end();) {
            if (dead.count(it->get())) {
                it = insts.erase(it);
            } else {
                ++it;
            }
        }
    }
}

std::optional<int> eval_binary(Opcode op, int a, int b) {
    switch (op) {
        case Opcode::Add: return a + b;
        case Opcode::Sub: return a - b;
        case Opcode::Mul: return a * b;
        case Opcode::Sdiv: if (b == 0) return std::nullopt; return a / b;
        case Opcode::Srem: if (b == 0) return std::nullopt; return a % b;
        case Opcode::ICmpEq: return a == b ? 1 : 0;
        case Opcode::ICmpNe: return a != b ? 1 : 0;
        case Opcode::ICmpSlt: return a < b ? 1 : 0;
        case Opcode::ICmpSgt: return a > b ? 1 : 0;
        case Opcode::ICmpSle: return a <= b ? 1 : 0;
        case Opcode::ICmpSge: return a >= b ? 1 : 0;
        default: return std::nullopt;
    }
}

}  // namespace

// Filled in by Tasks 2-5; wired by Task 6.
bool constprop(Function& fn) {
    bool changed = false;
    bool local_changed = true;
    while (local_changed) {
        local_changed = false;
        std::unordered_set<Instruction*> dead;
        for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
            for (const std::unique_ptr<Instruction>& inst_owner : owner->insts()) {
                Instruction* inst = inst_owner.get();
                if (!inst->has_result()) continue;
                Opcode op = inst->opcode();
                ConstantInt* folded = nullptr;

                if (op == Opcode::Phi) {
                    if (inst->num_operands() == 0) continue;
                    Value* first = inst->operand(0);
                    if (first->value_kind() != ValueKind::Constant) continue;
                    int v = static_cast<ConstantInt*>(first)->value();
                    bool same = true;
                    for (unsigned k = 1; k < inst->num_operands(); ++k) {
                        Value* o = inst->operand(k);
                        if (o->value_kind() != ValueKind::Constant ||
                            static_cast<ConstantInt*>(o)->value() != v) { same = false; break; }
                    }
                    if (same) folded = fn.module()->get_constant(v);
                } else if (op == Opcode::Neg) {
                    Value* o = inst->operand(0);
                    if (o->value_kind() == ValueKind::Constant) {
                        folded = fn.module()->get_constant(-static_cast<ConstantInt*>(o)->value());
                    }
                } else if (inst->num_operands() == 2) {
                    Value* a = inst->operand(0);
                    Value* b = inst->operand(1);
                    if (a->value_kind() == ValueKind::Constant &&
                        b->value_kind() == ValueKind::Constant) {
                        auto r = eval_binary(op,
                            static_cast<ConstantInt*>(a)->value(),
                            static_cast<ConstantInt*>(b)->value());
                        if (r) folded = fn.module()->get_constant(*r);
                    }
                }

                if (folded) {
                    inst->replace_all_uses_with(folded);
                    dead.insert(inst);
                    changed = local_changed = true;
                }
            }
        }
        if (!dead.empty()) erase_dead(fn, dead);
    }
    return changed;
}
bool dce(Function& /*fn*/) { return false; }
bool gvn(Function& /*fn*/) { return false; }
bool cfs(Function& /*fn*/) { return false; }

bool run_optim(Module& /*module*/) { return false; }

}  // namespace toyc
