#include "toyc/optim.h"

#include "toyc/ir.h"
#include "toyc/mem2reg.h"

#include <cstdint>
#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

int fold_shl(int value, unsigned amount) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    return static_cast<std::int32_t>(bits << amount);
}

int fold_ashr(int value, unsigned amount) {
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    if (amount == 0) return value;
    std::uint32_t shifted = bits >> amount;
    if ((bits & 0x80000000u) != 0) {
        shifted |= ~std::uint32_t{0} << (32u - amount);
    }
    return static_cast<std::int32_t>(shifted);
}

bool is_power_of_two_positive(int value, unsigned& amount) {
    if (value <= 0) return false;
    const std::uint32_t bits = static_cast<std::uint32_t>(value);
    if ((bits & (bits - 1u)) != 0) return false;
    amount = 0;
    std::uint32_t cursor = bits;
    while (cursor > 1u) {
        cursor >>= 1u;
        ++amount;
    }
    return true;
}

bool dominates(BasicBlock* dominator, BasicBlock* block,
               const DominatorTree& dt) {
    BasicBlock* cursor = block;
    while (true) {
        if (cursor == dominator) return true;
        BasicBlock* parent = dt.idom(cursor);
        if (!parent || parent == cursor) return false;
        cursor = parent;
    }
}

struct NaturalLoop {
    BasicBlock* header = nullptr;
    BasicBlock* preheader = nullptr;
    std::unordered_set<BasicBlock*> blocks;
};

std::vector<NaturalLoop> find_natural_loops(Function& fn, DominatorTree& dt) {
    std::unordered_map<BasicBlock*, std::unordered_set<BasicBlock*>> by_header;
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        BasicBlock* latch = owner.get();
        for (BasicBlock* successor : dt.succs(latch)) {
            if (!dominates(successor, latch, dt)) continue;
            auto& loop = by_header[successor];
            loop.insert(successor);
            loop.insert(latch);
            std::vector<BasicBlock*> work = {latch};
            while (!work.empty()) {
                BasicBlock* block = work.back();
                work.pop_back();
                for (BasicBlock* pred : dt.preds(block)) {
                    if (loop.insert(pred).second && pred != successor) {
                        work.push_back(pred);
                    }
                }
            }
        }
    }

    std::vector<NaturalLoop> result;
    for (auto& [header, members] : by_header) {
        std::vector<BasicBlock*> outside;
        for (BasicBlock* pred : dt.preds(header)) {
            if (!members.count(pred)) outside.push_back(pred);
        }
        BasicBlock* preheader = nullptr;
        if (outside.size() == 1) {
            Instruction* term = outside.front()->terminator();
            if (term && term->opcode() == Opcode::Br &&
                term->operand(0) == header) {
                preheader = outside.front();
            }
        }
        result.push_back({header, preheader, std::move(members)});
    }
    std::sort(result.begin(), result.end(), [](const NaturalLoop& lhs,
                                                const NaturalLoop& rhs) {
        return lhs.blocks.size() < rhs.blocks.size();
    });
    return result;
}

bool is_licm_candidate(Opcode opcode) {
    switch (opcode) {
        case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
        case Opcode::Sdiv: case Opcode::Srem: case Opcode::Neg:
        case Opcode::ICmpEq: case Opcode::ICmpNe: case Opcode::ICmpSlt:
        case Opcode::ICmpSgt: case Opcode::ICmpSle: case Opcode::ICmpSge:
        case Opcode::Shl: case Opcode::Shr:
            return true;
        default:
            return false;
    }
}

struct GvnKey {
    int op;
    std::uintptr_t a;
    std::uintptr_t b;
    bool operator==(const GvnKey&) const = default;
};

struct GvnKeyHash {
    std::size_t operator()(const GvnKey& k) const noexcept {
        return static_cast<std::size_t>(k.op) * 1315423911u ^
               (k.a * 2654435761u) ^ (k.b * 40503u);
    }
};

bool gvn_cseable(Opcode op) {
    switch (op) {
        case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
        case Opcode::Sdiv: case Opcode::Srem:
        case Opcode::ICmpEq: case Opcode::ICmpNe: case Opcode::ICmpSlt:
        case Opcode::ICmpSgt: case Opcode::ICmpSle: case Opcode::ICmpSge:
        case Opcode::Neg:
            return true;
        default:
            return false;
    }
}

GvnKey gvn_key(Instruction* inst) {
    Opcode op = inst->opcode();
    std::uintptr_t a = reinterpret_cast<std::uintptr_t>(inst->operand(0));
    std::uintptr_t b = inst->num_operands() >= 2
                           ? reinterpret_cast<std::uintptr_t>(inst->operand(1))
                           : 0;
    if (op == Opcode::ICmpSgt) { op = Opcode::ICmpSlt; std::swap(a, b); }
    if (op == Opcode::ICmpSge) { op = Opcode::ICmpSle; std::swap(a, b); }
    if (op == Opcode::Add || op == Opcode::Mul ||
        op == Opcode::ICmpEq || op == Opcode::ICmpNe) {
        if (a > b) std::swap(a, b);
    }
    return {static_cast<int>(op), a, b};
}

void gvn_walk(BasicBlock* bb, DominatorTree& dt,
              std::unordered_map<GvnKey, Value*, GvnKeyHash>& avail,
              std::unordered_set<Instruction*>& dead, bool& changed) {
    std::vector<GvnKey> inserted;
    for (const std::unique_ptr<Instruction>& inst_owner : bb->insts()) {
        Instruction* inst = inst_owner.get();
        if (!gvn_cseable(inst->opcode())) continue;
        GvnKey k = gvn_key(inst);
        auto it = avail.find(k);
        if (it != avail.end()) {
            inst->replace_all_uses_with(it->second);
            dead.insert(inst);
            changed = true;
        } else {
            avail.emplace(k, inst);
            inserted.push_back(k);
        }
    }
    for (BasicBlock* c : dt.children(bb)) gvn_walk(c, dt, avail, dead, changed);
    for (const GvnKey& k : inserted) avail.erase(k);
}

bool cfs_fold_branches(Function& fn) {
    bool changed = false;
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        BasicBlock* bb = owner.get();
        Instruction* term = bb->terminator();
        if (!term || term->opcode() != Opcode::CondBr) continue;
        Value* cond = term->operand(0);
        if (cond->value_kind() != ValueKind::Constant) continue;
        int cv = static_cast<ConstantInt*>(cond)->value();
        BasicBlock* taken = static_cast<BasicBlock*>(cv != 0 ? term->operand(1) : term->operand(2));
        std::list<std::unique_ptr<Instruction>>& insts = bb->insts();
        for (unsigned k = 0; k < term->num_operands(); ++k) term->operand(k)->remove_use(term);
        insts.pop_back();  // destroy the old CondBr
        insts.push_back(std::make_unique<BrInst>(taken));
        changed = true;
    }
    return changed;
}

bool cfs_remove_unreachable(Function& fn) {
    BasicBlock* entry = fn.entry();
    if (!entry) return false;
    std::unordered_set<BasicBlock*> reach;
    std::vector<BasicBlock*> stk = {entry};
    while (!stk.empty()) {
        BasicBlock* bb = stk.back();
        stk.pop_back();
        if (!reach.insert(bb).second) continue;
        Instruction* term = bb->terminator();
        if (!term) continue;
        if (term->opcode() == Opcode::Br) {
            stk.push_back(static_cast<BasicBlock*>(term->operand(0)));
        } else if (term->opcode() == Opcode::CondBr) {
            stk.push_back(static_cast<BasicBlock*>(term->operand(1)));
            stk.push_back(static_cast<BasicBlock*>(term->operand(2)));
        }
    }
    // Drop phi incomings that come from now-unreachable preds.
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        BasicBlock* bb = owner.get();
        if (!reach.count(bb)) continue;
        for (const std::unique_ptr<Instruction>& inst : bb->insts()) {
            if (inst->opcode() != Opcode::Phi) break;  // phis live only at block front
            std::vector<BasicBlock*> keep;
            for (BasicBlock* p : static_cast<PhiInst*>(inst.get())->incoming_blocks()) {
                if (reach.count(p)) keep.push_back(p);
            }
            if (keep.size() != inst->num_operands()) {
                static_cast<PhiInst*>(inst.get())->reorder_incoming(keep);
            }
        }
    }
    bool changed = false;
    std::list<std::unique_ptr<BasicBlock>>& blocks = fn.blocks();
    for (auto it = blocks.begin(); it != blocks.end();) {
        if (!reach.count(it->get())) { it = blocks.erase(it); changed = true; }
        else ++it;
    }
    return changed;
}

bool cfs_simplify_phi(Function& fn) {
    std::unordered_set<Instruction*> dead;
    bool changed = false;
    bool loop = true;
    while (loop) {
        loop = false;
        for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
            for (const std::unique_ptr<Instruction>& inst_owner : owner->insts()) {
                Instruction* inst = inst_owner.get();
                if (inst->opcode() != Opcode::Phi || dead.count(inst)) continue;
                Value* rep = nullptr;
                if (inst->num_operands() == 1) {
                    rep = inst->operand(0);
                } else if (inst->num_operands() > 1) {
                    Value* first = inst->operand(0);
                    bool same = true;
                    for (unsigned k = 1; k < inst->num_operands(); ++k) {
                        if (inst->operand(k) != first) { same = false; break; }
                    }
                    if (same) rep = first;
                }
                if (rep && rep != inst) {
                    inst->replace_all_uses_with(rep);
                    dead.insert(inst);
                    changed = loop = true;
                }
            }
        }
    }
    if (!dead.empty()) erase_dead(fn, dead);
    return changed;
}

bool cfs_merge_blocks(Function& fn) {
    bool changed = false;
    bool loop = true;
    while (loop) {
        loop = false;
        DominatorTree dt;
        dt.analyze(fn);
        BasicBlock* a = nullptr;
        BasicBlock* b = nullptr;
        for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
            BasicBlock* bb = owner.get();
            Instruction* term = bb->terminator();
            if (!term || term->opcode() != Opcode::Br) continue;
            BasicBlock* succ = static_cast<BasicBlock*>(term->operand(0));
            if (dt.preds(succ).size() != 1) continue;  // succ must have only this predecessor
            a = bb;
            b = succ;
            break;
        }
        if (!a) break;
        // b must not start with a phi (single-pred blocks have none after simplify_phi).
        bool b_has_phi = false;
        for (const std::unique_ptr<Instruction>& inst : b->insts()) {
            if (inst->opcode() == Opcode::Phi) { b_has_phi = true; break; }
        }
        if (b_has_phi) break;
        Instruction* b_term = b->terminator();
        std::vector<BasicBlock*> b_succs;
        if (b_term && b_term->opcode() == Opcode::Br) {
            b_succs.push_back(static_cast<BasicBlock*>(b_term->operand(0)));
        } else if (b_term && b_term->opcode() == Opcode::CondBr) {
            b_succs.push_back(static_cast<BasicBlock*>(b_term->operand(1)));
            b_succs.push_back(static_cast<BasicBlock*>(b_term->operand(2)));
        }
        for (BasicBlock* succ : b_succs) {
            for (const std::unique_ptr<Instruction>& inst : succ->insts()) {
                if (inst->opcode() != Opcode::Phi) break;
                static_cast<PhiInst*>(inst.get())->replace_incoming_block(b, a);
            }
        }
        Instruction* br = a->terminator();
        for (unsigned k = 0; k < br->num_operands(); ++k) br->operand(k)->remove_use(br);
        a->insts().pop_back();  // drop a's br
        std::list<std::unique_ptr<Instruction>>& b_insts = b->insts();
        while (!b_insts.empty()) {
            auto it = b_insts.begin();
            (*it)->set_parent(a);
            a->insts().push_back(std::move(*it));
            b_insts.erase(it);
        }
        std::list<std::unique_ptr<BasicBlock>>& blocks = fn.blocks();
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == b) { blocks.erase(it); break; }
        }
        changed = loop = true;
    }
    return changed;
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
                } else if (op == Opcode::Shl || op == Opcode::Shr) {
                    Value* o = inst->operand(0);
                    if (o->value_kind() == ValueKind::Constant) {
                        const int value = static_cast<ConstantInt*>(o)->value();
                        const unsigned amount = op == Opcode::Shl
                            ? static_cast<ShlInst*>(inst)->amount()
                            : static_cast<ShrInst*>(inst)->amount();
                        folded = fn.module()->get_constant(
                            op == Opcode::Shl ? fold_shl(value, amount)
                                              : fold_ashr(value, amount));
                    }
                } else if (op != Opcode::Call && inst->num_operands() == 2) {
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

bool algebraic_simplify(Function& fn) {
    std::unordered_set<Instruction*> dead;
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        for (const std::unique_ptr<Instruction>& inst_owner : owner->insts()) {
            Instruction* inst = inst_owner.get();
            Value* replacement = nullptr;
            auto constant_value = [](Value* value) -> std::optional<int> {
                if (value->value_kind() != ValueKind::Constant) return std::nullopt;
                return static_cast<ConstantInt*>(value)->value();
            };

            if (inst->num_operands() == 2) {
                Value* lhs = inst->operand(0);
                Value* rhs = inst->operand(1);
                const std::optional<int> left = constant_value(lhs);
                const std::optional<int> right = constant_value(rhs);
                switch (inst->opcode()) {
                    case Opcode::Add:
                        if (right == 0) replacement = lhs;
                        else if (left == 0) replacement = rhs;
                        break;
                    case Opcode::Sub:
                        if (right == 0) replacement = lhs;
                        else if (lhs == rhs) replacement = fn.module()->get_constant(0);
                        break;
                    case Opcode::Mul:
                        if (right == 0 || left == 0) replacement = fn.module()->get_constant(0);
                        else if (right == 1) replacement = lhs;
                        else if (left == 1) replacement = rhs;
                        break;
                    case Opcode::Sdiv:
                        if (right == 1) replacement = lhs;
                        break;
                    case Opcode::Srem:
                        if (right == 1) replacement = fn.module()->get_constant(0);
                        break;
                    case Opcode::ICmpEq:
                    case Opcode::ICmpSle:
                    case Opcode::ICmpSge:
                        if (lhs == rhs) replacement = fn.module()->get_constant(1);
                        break;
                    case Opcode::ICmpNe:
                    case Opcode::ICmpSlt:
                    case Opcode::ICmpSgt:
                        if (lhs == rhs) replacement = fn.module()->get_constant(0);
                        break;
                    default:
                        break;
                }
            } else if (inst->opcode() == Opcode::Neg && inst->num_operands() == 1) {
                Instruction* operand = dynamic_cast<Instruction*>(inst->operand(0));
                if (operand && operand->opcode() == Opcode::Neg) {
                    replacement = operand->operand(0);
                }
            }

            if (replacement && replacement != inst) {
                inst->replace_all_uses_with(replacement);
                dead.insert(inst);
            }
        }
    }
    if (dead.empty()) return false;
    erase_dead(fn, dead);
    return true;
}

bool strength_reduce(Function& fn) {
    bool changed = false;
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        BasicBlock* block = owner.get();
        std::list<std::unique_ptr<Instruction>>& insts = block->insts();
        for (auto it = insts.begin(); it != insts.end();) {
            Instruction* inst = it->get();
            if (inst->opcode() != Opcode::Mul || inst->num_operands() != 2) {
                ++it;
                continue;
            }
            Value* value = nullptr;
            ConstantInt* constant = nullptr;
            if (inst->operand(0)->value_kind() == ValueKind::Constant) {
                constant = static_cast<ConstantInt*>(inst->operand(0));
                value = inst->operand(1);
            } else if (inst->operand(1)->value_kind() == ValueKind::Constant) {
                constant = static_cast<ConstantInt*>(inst->operand(1));
                value = inst->operand(0);
            }
            unsigned amount = 0;
            if (!constant || !is_power_of_two_positive(constant->value(), amount) ||
                amount == 0) {
                ++it;
                continue;
            }

            auto replacement =
                std::make_unique<ShlInst>(value, amount, fn.module()->fresh_id());
            ShlInst* raw = replacement.get();
            raw->set_parent(block);
            insts.insert(it, std::move(replacement));
            inst->replace_all_uses_with(raw);
            for (unsigned k = 0; k < inst->num_operands(); ++k) {
                inst->operand(k)->remove_use(inst);
            }
            it = insts.erase(it);
            changed = true;
        }
    }
    return changed;
}

bool licm(Function& fn) {
    if (!fn.entry()) return false;
    DominatorTree dt;
    dt.analyze(fn);
    std::vector<NaturalLoop> loops = find_natural_loops(fn, dt);
    bool changed = false;
    for (NaturalLoop& loop : loops) {
        if (!loop.preheader) continue;
        std::unordered_set<Value*> invariant;
        bool local_changed = true;
        while (local_changed) {
            local_changed = false;
            for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
                BasicBlock* block = owner.get();
                if (!loop.blocks.count(block) || block == loop.preheader) continue;
                for (const std::unique_ptr<Instruction>& inst_owner : block->insts()) {
                    Instruction* inst = inst_owner.get();
                    if (!is_licm_candidate(inst->opcode()) || invariant.count(inst)) {
                        continue;
                    }
                    bool operands_invariant = true;
                    for (Value* operand : inst->operands()) {
                        Instruction* definition = dynamic_cast<Instruction*>(operand);
                        if (definition && loop.blocks.count(definition->parent()) &&
                            !invariant.count(definition)) {
                            operands_invariant = false;
                            break;
                        }
                    }
                    if (operands_invariant) {
                        invariant.insert(inst);
                        local_changed = true;
                    }
                }
            }
        }
        if (invariant.empty()) continue;

        std::vector<Instruction*> ordered;
        std::unordered_set<Instruction*> scheduled;
        while (ordered.size() < invariant.size()) {
            bool progress = false;
            for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
                for (const std::unique_ptr<Instruction>& inst_owner : owner->insts()) {
                    Instruction* inst = inst_owner.get();
                    if (!invariant.count(inst) || scheduled.count(inst)) continue;
                    bool ready = true;
                    for (Value* operand : inst->operands()) {
                        Instruction* definition = dynamic_cast<Instruction*>(operand);
                        if (definition && invariant.count(definition) &&
                            !scheduled.count(definition)) {
                            ready = false;
                            break;
                        }
                    }
                    if (!ready) continue;
                    scheduled.insert(inst);
                    ordered.push_back(inst);
                    progress = true;
                }
            }
            if (!progress) break;
        }

        std::list<std::unique_ptr<Instruction>>& destination = loop.preheader->insts();
        auto insertion = destination.end();
        if (loop.preheader->terminator()) insertion = std::prev(destination.end());
        for (Instruction* inst : ordered) {
            BasicBlock* block = inst->parent();
            std::list<std::unique_ptr<Instruction>>& source = block->insts();
            for (auto it = source.begin(); it != source.end(); ++it) {
                if (it->get() != inst) continue;
                inst->set_parent(loop.preheader);
                destination.splice(insertion, source, it);
                changed = true;
                break;
            }
        }
    }
    return changed;
}

bool eliminate_tail_recursion(Function& fn) {
    struct TailSite {
        BasicBlock* block;
        CallInst* call;
    };
    std::vector<TailSite> sites;
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        BasicBlock* block = owner.get();
        std::list<std::unique_ptr<Instruction>>& insts = block->insts();
        if (insts.size() < 2) continue;
        auto ret_it = std::prev(insts.end());
        auto call_it = std::prev(ret_it);
        Instruction* ret = ret_it->get();
        Instruction* candidate = call_it->get();
        if (ret->opcode() != Opcode::Ret || candidate->opcode() != Opcode::Call) continue;
        CallInst* call = static_cast<CallInst*>(candidate);
        if (call->callee_name() != fn.short_name() ||
            call->num_operands() != fn.params().size()) continue;

        const bool int_tail = fn.ret_type() == FuncRet::Int && call->has_result() &&
                              ret->num_operands() == 1 && ret->operand(0) == call &&
                              call->uses().size() == 1;
        const bool void_tail = fn.ret_type() == FuncRet::Void && !call->has_result() &&
                               ret->num_operands() == 0;
        if (int_tail || void_tail) sites.push_back({block, call});
    }
    if (sites.empty() || !fn.entry()) return false;

    BasicBlock* loop_header = fn.entry();
    BasicBlock* new_entry = fn.create_block();
    new_entry->push_back(std::make_unique<BrInst>(loop_header));
    std::list<std::unique_ptr<BasicBlock>>& blocks = fn.blocks();
    auto new_entry_it = std::prev(blocks.end());
    blocks.splice(blocks.begin(), blocks, new_entry_it);

    std::vector<PhiInst*> parameter_phis;
    parameter_phis.reserve(fn.params().size());
    for (const std::unique_ptr<Value>& param_owner : fn.params()) {
        Value* param = param_owner.get();
        auto phi = std::make_unique<PhiInst>(fn.module()->fresh_id());
        PhiInst* raw = phi.get();
        param->replace_all_uses_with(raw);
        raw->add_incoming(param, new_entry);
        loop_header->push_front(std::move(phi));
        parameter_phis.push_back(raw);
    }

    for (const TailSite& site : sites) {
        for (unsigned i = 0; i < site.call->num_operands(); ++i) {
            parameter_phis[i]->add_incoming(site.call->operand(i), site.block);
        }
        std::list<std::unique_ptr<Instruction>>& insts = site.block->insts();
        for (const std::unique_ptr<Instruction>& owner : insts) {
            Instruction* inst = owner.get();
            if (inst != site.call && inst->opcode() != Opcode::Ret) continue;
            for (unsigned i = 0; i < inst->num_operands(); ++i) {
                if (Value* operand = inst->operand(i)) operand->remove_use(inst);
            }
        }
        for (auto it = insts.begin(); it != insts.end();) {
            Instruction* inst = it->get();
            if (inst == site.call || inst->opcode() == Opcode::Ret) {
                it = insts.erase(it);
            } else {
                ++it;
            }
        }
        site.block->push_back(std::make_unique<BrInst>(loop_header));
    }
    return true;
}

bool limited_inline(Module& module) {
    std::unordered_map<std::string, Function*> functions;
    std::size_t original_instruction_count = 0;
    for (const std::unique_ptr<Function>& fn : module.functions()) {
        functions.emplace(fn->short_name(), fn.get());
        for (const std::unique_ptr<BasicBlock>& block : fn->blocks()) {
            original_instruction_count += block->insts().size();
        }
    }
    const std::size_t growth_budget = original_instruction_count;
    std::size_t inserted_count = 0;
    bool changed = false;

    auto eligible = [](const Function& callee) {
        if (callee.blocks().size() != 1 || !callee.entry()) return false;
        if (callee.entry()->insts().size() > 12) return false;
        for (const std::unique_ptr<Instruction>& inst : callee.entry()->insts()) {
            switch (inst->opcode()) {
                case Opcode::Add: case Opcode::Sub: case Opcode::Mul:
                case Opcode::Sdiv: case Opcode::Srem: case Opcode::Neg:
                case Opcode::ICmpEq: case Opcode::ICmpNe: case Opcode::ICmpSlt:
                case Opcode::ICmpSgt: case Opcode::ICmpSle: case Opcode::ICmpSge:
                case Opcode::Shl: case Opcode::Shr: case Opcode::Ret:
                    break;
                default:
                    return false;
            }
        }
        Instruction* term = callee.entry()->terminator();
        return term && term->opcode() == Opcode::Ret;
    };

    auto mapped = [](Value* value,
                     const std::unordered_map<Value*, Value*>& values) -> Value* {
        auto found = values.find(value);
        return found == values.end() ? value : found->second;
    };

    for (const std::unique_ptr<Function>& caller_owner : module.functions()) {
        Function& caller = *caller_owner;
        // Keep the entry function structurally compiled.  Besides making the
        // optimization boundary easy to audit, this prevents a chain of
        // otherwise ordinary inlining/folding passes from degenerating into
        // whole-program evaluation of ToyC's closed main function.
        if (caller.short_name() == "main") continue;
        for (const std::unique_ptr<BasicBlock>& block_owner : caller.blocks()) {
            BasicBlock* block = block_owner.get();
            std::list<std::unique_ptr<Instruction>>& insts = block->insts();
            for (auto it = insts.begin(); it != insts.end();) {
                Instruction* inst = it->get();
                if (inst->opcode() != Opcode::Call) {
                    ++it;
                    continue;
                }
                CallInst* call = static_cast<CallInst*>(inst);
                auto found = functions.find(call->callee_name());
                if (found == functions.end() || found->second == &caller ||
                    !eligible(*found->second) ||
                    found->second->params().size() != call->num_operands()) {
                    ++it;
                    continue;
                }
                Function& callee = *found->second;
                const std::size_t clone_count = callee.entry()->insts().size() - 1;
                if (inserted_count + clone_count > growth_budget) {
                    ++it;
                    continue;
                }

                std::unordered_map<Value*, Value*> values;
                for (unsigned i = 0; i < call->num_operands(); ++i) {
                    values.emplace(callee.param(i), call->operand(i));
                }
                Value* return_value = nullptr;
                for (const std::unique_ptr<Instruction>& source_owner :
                     callee.entry()->insts()) {
                    Instruction* source = source_owner.get();
                    if (source->opcode() == Opcode::Ret) {
                        if (source->num_operands() == 1) {
                            return_value = mapped(source->operand(0), values);
                        }
                        break;
                    }
                    std::unique_ptr<Instruction> clone;
                    if (source->opcode() == Opcode::Neg) {
                        clone = std::make_unique<NegInst>(
                            mapped(source->operand(0), values), module.fresh_id());
                    } else if (source->opcode() == Opcode::Shl) {
                        clone = std::make_unique<ShlInst>(
                            mapped(source->operand(0), values),
                            static_cast<ShlInst*>(source)->amount(), module.fresh_id());
                    } else if (source->opcode() == Opcode::Shr) {
                        clone = std::make_unique<ShrInst>(
                            mapped(source->operand(0), values),
                            static_cast<ShrInst*>(source)->amount(), module.fresh_id());
                    } else if (source->opcode() >= Opcode::ICmpEq &&
                               source->opcode() <= Opcode::ICmpSge) {
                        clone = std::make_unique<ICmpInst>(
                            source->opcode(), mapped(source->operand(0), values),
                            mapped(source->operand(1), values), module.fresh_id());
                    } else {
                        clone = std::make_unique<BinaryInst>(
                            source->opcode(), mapped(source->operand(0), values),
                            mapped(source->operand(1), values), module.fresh_id());
                    }
                    Instruction* raw = clone.get();
                    raw->set_parent(block);
                    insts.insert(it, std::move(clone));
                    values.emplace(source, raw);
                }
                if (call->has_result()) {
                    if (!return_value) {
                        ++it;
                        continue;
                    }
                    call->replace_all_uses_with(return_value);
                }
                for (unsigned i = 0; i < call->num_operands(); ++i) {
                    call->operand(i)->remove_use(call);
                }
                it = insts.erase(it);
                inserted_count += clone_count;
                changed = true;
            }
        }
    }
    return changed;
}

bool localize_globals(Module& module) {
    struct FunctionEffects {
        std::unordered_set<GlobalAddr*> touched;
        std::vector<std::string> callees;
        bool unknown_call = false;
    };
    std::unordered_map<std::string, FunctionEffects> effects;
    std::unordered_set<std::string> known_functions;
    for (const std::unique_ptr<Function>& fn : module.functions()) {
        known_functions.insert(fn->short_name());
    }
    for (const std::unique_ptr<Function>& fn : module.functions()) {
        FunctionEffects& effect = effects[fn->short_name()];
        for (const std::unique_ptr<BasicBlock>& block : fn->blocks()) {
            for (const std::unique_ptr<Instruction>& inst : block->insts()) {
                if (inst->opcode() == Opcode::Call) {
                    const std::string& callee =
                        static_cast<CallInst*>(inst.get())->callee_name();
                    effect.callees.push_back(callee);
                    if (!known_functions.count(callee)) effect.unknown_call = true;
                }
                if ((inst->opcode() == Opcode::Load ||
                     inst->opcode() == Opcode::Store) &&
                    inst->num_operands() > 0 &&
                    inst->operand(0)->value_kind() == ValueKind::GlobalAddr) {
                    effect.touched.insert(
                        static_cast<GlobalAddr*>(inst->operand(0)));
                }
            }
        }
    }
    bool summaries_changed = true;
    while (summaries_changed) {
        summaries_changed = false;
        for (auto& [name, effect] : effects) {
            for (const std::string& callee : effect.callees) {
                auto found = effects.find(callee);
                if (found == effects.end()) continue;
                const std::size_t before = effect.touched.size();
                const bool before_unknown = effect.unknown_call;
                effect.touched.insert(found->second.touched.begin(),
                                      found->second.touched.end());
                effect.unknown_call |= found->second.unknown_call;
                summaries_changed |= effect.touched.size() != before ||
                                     effect.unknown_call != before_unknown;
            }
        }
    }

    bool changed = false;
    for (const std::unique_ptr<Function>& fn_owner : module.functions()) {
        Function& fn = *fn_owner;
        if (!fn.entry()) continue;

        bool has_fallthrough_exit = false;
        bool unknown_callee = false;
        std::unordered_set<GlobalAddr*> callee_touches;
        struct GlobalAccess {
            GlobalAddr* address = nullptr;
            bool written = false;
        };
        std::unordered_map<GlobalAddr*, GlobalAccess> accesses;
        for (const std::unique_ptr<BasicBlock>& block : fn.blocks()) {
            if (!block->is_terminated()) has_fallthrough_exit = true;
            for (const std::unique_ptr<Instruction>& inst : block->insts()) {
                if (inst->opcode() == Opcode::Call) {
                    const std::string& callee =
                        static_cast<CallInst*>(inst.get())->callee_name();
                    auto found = effects.find(callee);
                    if (found == effects.end() || found->second.unknown_call) {
                        unknown_callee = true;
                    } else {
                        callee_touches.insert(found->second.touched.begin(),
                                              found->second.touched.end());
                    }
                }
                if ((inst->opcode() == Opcode::Load || inst->opcode() == Opcode::Store) &&
                    inst->num_operands() > 0 &&
                    inst->operand(0)->value_kind() == ValueKind::GlobalAddr) {
                    GlobalAddr* address = static_cast<GlobalAddr*>(inst->operand(0));
                    GlobalAccess& access = accesses[address];
                    access.address = address;
                    if (inst->opcode() == Opcode::Store) access.written = true;
                }
            }
        }
        if (accesses.empty()) continue;

        BasicBlock* entry = fn.entry();
        auto insertion = entry->insts().begin();
        for (auto& [address, access] : accesses) {
            if (unknown_callee || callee_touches.count(address)) continue;
            if (access.written && has_fallthrough_exit) continue;

            auto slot_owner = std::make_unique<AllocaInst>(module.fresh_id());
            AllocaInst* slot = slot_owner.get();
            slot->set_parent(entry);
            insertion = std::next(entry->insts().insert(insertion, std::move(slot_owner)));

            auto initial_load_owner = std::make_unique<LoadInst>(address, module.fresh_id());
            LoadInst* initial_load = initial_load_owner.get();
            initial_load->set_parent(entry);
            insertion = std::next(
                entry->insts().insert(insertion, std::move(initial_load_owner)));

            auto initial_store_owner =
                std::make_unique<StoreInst>(slot, initial_load);
            initial_store_owner->set_parent(entry);
            insertion = std::next(
                entry->insts().insert(insertion, std::move(initial_store_owner)));

            for (const std::unique_ptr<BasicBlock>& block : fn.blocks()) {
                for (const std::unique_ptr<Instruction>& inst : block->insts()) {
                    if (inst.get() == initial_load) continue;
                    if ((inst->opcode() == Opcode::Load ||
                         inst->opcode() == Opcode::Store) &&
                        inst->num_operands() > 0 && inst->operand(0) == address) {
                        inst->set_operand(0, slot);
                    }
                }
            }

            if (access.written) {
                for (const std::unique_ptr<BasicBlock>& block : fn.blocks()) {
                    std::list<std::unique_ptr<Instruction>>& insts = block->insts();
                    for (auto it = insts.begin(); it != insts.end(); ++it) {
                        if ((*it)->opcode() != Opcode::Ret) continue;
                        auto final_load_owner =
                            std::make_unique<LoadInst>(slot, module.fresh_id());
                        LoadInst* final_load = final_load_owner.get();
                        final_load->set_parent(block.get());
                        insts.insert(it, std::move(final_load_owner));
                        auto final_store_owner =
                            std::make_unique<StoreInst>(address, final_load);
                        final_store_owner->set_parent(block.get());
                        insts.insert(it, std::move(final_store_owner));
                        break;
                    }
                }
            }
            changed = true;
        }
    }
    return changed;
}

bool interprocedural_global_opt(Module& module) {
    return localize_globals(module);
}

bool dce(Function& fn) {
    std::unordered_set<Instruction*> live;
    std::vector<Instruction*> work;
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        for (const std::unique_ptr<Instruction>& inst_owner : owner->insts()) {
            Instruction* inst = inst_owner.get();
            Opcode op = inst->opcode();
            bool essential = (op == Opcode::Store || op == Opcode::Call ||
                              op == Opcode::Ret || op == Opcode::Br || op == Opcode::CondBr);
            if (essential) {
                live.insert(inst);
                work.push_back(inst);
            }
        }
    }
    while (!work.empty()) {
        Instruction* inst = work.back();
        work.pop_back();
        for (unsigned k = 0; k < inst->num_operands(); ++k) {
            Value* op = inst->operand(k);
            if (!op) continue;  // null operand (e.g. void ret)
            if (op->value_kind() != ValueKind::Register) continue;  // constants/params/blocks: not instructions
            // Not all ValueKind::Register are Instructions (e.g. Module::create_register)
            // Check if it's actually an instruction by checking if it has an opcode (Instructions do)
            Instruction* def = dynamic_cast<Instruction*>(op);
            if (!def) continue;  // skip non-instruction registers
            if (!live.count(def)) {
                live.insert(def);
                work.push_back(def);
            }
        }
    }
    std::unordered_set<Instruction*> dead;
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        for (const std::unique_ptr<Instruction>& inst_owner : owner->insts()) {
            if (!live.count(inst_owner.get())) dead.insert(inst_owner.get());
        }
    }
    if (dead.empty()) return false;
    erase_dead(fn, dead);
    return true;
}
bool gvn(Function& fn) {
    if (!fn.entry()) return false;
    DominatorTree dt;
    dt.analyze(fn);
    std::unordered_map<GvnKey, Value*, GvnKeyHash> avail;
    std::unordered_set<Instruction*> dead;
    bool changed = false;
    gvn_walk(fn.entry(), dt, avail, dead, changed);
    if (!dead.empty()) erase_dead(fn, dead);
    return changed;
}
bool cfs(Function& fn) {
    bool changed = false;
    changed |= cfs_fold_branches(fn);
    changed |= cfs_remove_unreachable(fn);
    changed |= cfs_simplify_phi(fn);
    changed |= cfs_merge_blocks(fn);
    return changed;
}

bool sccp(Function& fn) {
    bool any = false;
    for (int iteration = 0; iteration < 8; ++iteration) {
        bool changed = false;
        changed |= constprop(fn);
        changed |= cfs(fn);
        changed |= dce(fn);
        any |= changed;
        if (!changed) break;
    }
    return any;
}

bool run_optim(Module& module) {
    bool any = false;
    any |= limited_inline(module);
    for (const std::unique_ptr<Function>& fn : module.functions()) {
        any |= eliminate_tail_recursion(*fn);
    }
    for (int iter = 0; iter < 10; ++iter) {
        bool changed = false;
        for (const std::unique_ptr<Function>& fn : module.functions()) {
            changed |= sccp(*fn);
            changed |= algebraic_simplify(*fn);
            changed |= strength_reduce(*fn);
            changed |= licm(*fn);
            changed |= dce(*fn);
            changed |= gvn(*fn);
            changed |= cfs(*fn);
        }
        if (!changed) break;
        any = true;
    }
    return any;
}

}  // namespace toyc
