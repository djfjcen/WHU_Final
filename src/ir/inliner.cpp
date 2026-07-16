#include "toyc/inliner.h"

#include "toyc/ir.h"

#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace toyc {

namespace {

constexpr std::size_t kInlineInstLimit = 100;

using ValueMap = std::unordered_map<const Value*, Value*>;
using BlockMap = std::unordered_map<const BasicBlock*, BasicBlock*>;

Function* find_function(Module& module, const std::string& name) {
    for (const std::unique_ptr<Function>& fn : module.functions()) {
        if (fn->short_name() == name) return fn.get();
    }
    return nullptr;
}

bool is_leaf(const Function& fn) {
    for (const std::unique_ptr<BasicBlock>& block : fn.blocks()) {
        for (const std::unique_ptr<Instruction>& inst : block->insts()) {
            if (inst->opcode() == Opcode::Call) return false;
        }
    }
    return true;
}

std::size_t inst_count(const Function& fn) {
    std::size_t n = 0;
    for (const std::unique_ptr<BasicBlock>& block : fn.blocks()) {
        n += block->insts().size();
    }
    return n;
}

void successors(const BasicBlock* block, std::vector<BasicBlock*>& out) {
    const Instruction* term = block->terminator();
    if (!term) return;
    if (term->opcode() == Opcode::Br) {
        out.push_back(static_cast<BasicBlock*>(term->operand(0)));
    } else if (term->opcode() == Opcode::CondBr) {
        out.push_back(static_cast<BasicBlock*>(term->operand(1)));
        out.push_back(static_cast<BasicBlock*>(term->operand(2)));
    }
}

// A block is inside a loop iff it can reach itself along successor edges.
bool block_in_loop(const BasicBlock* block) {
    std::unordered_set<const BasicBlock*> seen;
    std::vector<BasicBlock*> stack;
    successors(block, stack);
    while (!stack.empty()) {
        BasicBlock* current = stack.back();
        stack.pop_back();
        if (current == block) return true;
        if (!seen.insert(current).second) continue;
        successors(current, stack);
    }
    return false;
}

// A callee is safe to clone in a single in-order pass iff every value operand
// is a constant, global, parameter, an alloca (cloned up front), or an
// instruction defined earlier in the SAME block. Cross-block SSA values or phis
// would break the single-pass clone, so such callees are simply not inlined.
bool is_inline_safe(const Function& fn) {
    if (!fn.entry()) return false;
    for (const std::unique_ptr<BasicBlock>& block : fn.blocks()) {
        if (!block->is_terminated()) return false;  // e.g. fall-through void tail
        std::unordered_set<const Instruction*> defined_here;
        for (const std::unique_ptr<Instruction>& inst : block->insts()) {
            const Opcode op = inst->opcode();
            if (op == Opcode::Phi || op == Opcode::Call) return false;
            for (unsigned k = 0; k < inst->num_operands(); ++k) {
                const Value* operand = inst->operand(k);
                switch (operand->value_kind()) {
                    case ValueKind::Constant:
                    case ValueKind::GlobalAddr:
                    case ValueKind::Param:
                    case ValueKind::BasicBlock:
                        break;
                    case ValueKind::Register: {
                        const auto* def = static_cast<const Instruction*>(operand);
                        if (def->opcode() == Opcode::Alloca) break;
                        if (defined_here.count(def)) break;
                        return false;
                    }
                    default:
                        return false;
                }
            }
            if (op != Opcode::Alloca) defined_here.insert(inst.get());
        }
    }
    return true;
}

class Inliner {
public:
    explicit Inliner(Module& module) : module_(module) {}

    bool run() {
        bool any = false;
        for (const std::unique_ptr<Function>& fn : module_.functions()) {
            Function* caller = fn.get();
            std::size_t guard = 0;
            while (guard++ < 100000 && inline_one(*caller)) {
                any = true;
            }
        }
        return any;
    }

private:
    // Find and inline a single eligible call site in `caller`; returns false
    // when none remains. Restarting after each inline keeps iteration simple
    // and safe against the block/instruction list mutations we perform.
    bool inline_one(Function& caller) {
        // Compliance guard: never let inlining collapse main into a constant.
        // We only inline into main at call sites inside a loop, so main always
        // keeps a runtime loop and is never reduced to a hard-coded result.
        const bool caller_is_main = caller.short_name() == "main";
        for (const std::unique_ptr<BasicBlock>& block : caller.blocks()) {
            for (const std::unique_ptr<Instruction>& inst : block->insts()) {
                if (inst->opcode() != Opcode::Call) continue;
                auto* call = static_cast<CallInst*>(inst.get());
                if (!call->has_result()) continue;  // only int-returning callees
                Function* callee = find_function(module_, call->callee_name());
                if (!callee || callee == &caller) continue;
                if (callee->ret_type() != FuncRet::Int) continue;
                if (call->num_operands() != callee->params().size()) continue;
                if (!is_leaf(*callee)) continue;
                if (inst_count(*callee) > kInlineInstLimit) continue;
                if (!is_inline_safe(*callee)) continue;
                if (caller_is_main && !block_in_loop(block.get())) continue;
                inline_call(caller, block.get(), call, *callee);
                return true;
            }
        }
        return false;
    }

    Value* remap(const Value* value, const ValueMap& values) const {
        switch (value->value_kind()) {
            case ValueKind::Constant:
            case ValueKind::GlobalAddr:
                return const_cast<Value*>(value);
            default:
                break;
        }
        auto found = values.find(value);
        return found == values.end() ? nullptr : found->second;
    }

    std::unique_ptr<Instruction> clone_value_inst(const Instruction& inst,
                                                  const ValueMap& values) {
        const unsigned id = module_.fresh_id();
        switch (inst.opcode()) {
            case Opcode::Load:
                return std::make_unique<LoadInst>(remap(inst.operand(0), values), id);
            case Opcode::Store:
                return std::make_unique<StoreInst>(remap(inst.operand(0), values),
                                                   remap(inst.operand(1), values));
            case Opcode::Add:
            case Opcode::Sub:
            case Opcode::Mul:
            case Opcode::Sdiv:
            case Opcode::Srem:
                return std::make_unique<BinaryInst>(inst.opcode(),
                                                    remap(inst.operand(0), values),
                                                    remap(inst.operand(1), values), id);
            case Opcode::ICmpEq:
            case Opcode::ICmpNe:
            case Opcode::ICmpSlt:
            case Opcode::ICmpSgt:
            case Opcode::ICmpSle:
            case Opcode::ICmpSge:
                return std::make_unique<ICmpInst>(inst.opcode(),
                                                  remap(inst.operand(0), values),
                                                  remap(inst.operand(1), values), id);
            case Opcode::Neg:
                return std::make_unique<NegInst>(remap(inst.operand(0), values), id);
            case Opcode::Shl:
                return std::make_unique<ShlInst>(remap(inst.operand(0), values),
                                                 static_cast<const ShlInst&>(inst).amount(), id);
            case Opcode::Shr:
                return std::make_unique<ShrInst>(remap(inst.operand(0), values),
                                                 static_cast<const ShrInst&>(inst).amount(), id);
            default:
                return nullptr;  // unreachable: filtered by is_inline_safe
        }
    }

    AllocaInst* clone_alloca_into_entry(Function& caller) {
        auto alloca = std::make_unique<AllocaInst>(module_.fresh_id());
        AllocaInst* raw = alloca.get();
        raw->set_parent(caller.entry());
        caller.entry()->insts().insert(caller.entry()->insts().begin(), std::move(alloca));
        return raw;
    }

    void inline_call(Function& caller, BasicBlock* site, CallInst* call,
                     Function& callee) {
        ValueMap values;
        for (unsigned i = 0; i < callee.params().size(); ++i) {
            values[callee.param(i)] = call->operand(i);
        }

        // Clone callee allocas into the caller entry so mem2reg (which only
        // scans the entry block) can still promote them.
        for (const std::unique_ptr<BasicBlock>& block : callee.blocks()) {
            for (const std::unique_ptr<Instruction>& inst : block->insts()) {
                if (inst->opcode() == Opcode::Alloca) {
                    values[inst.get()] = clone_alloca_into_entry(caller);
                }
            }
        }
        AllocaInst* result_slot = clone_alloca_into_entry(caller);

        // Fresh blocks: one per callee block, plus the continuation.
        BlockMap blocks;
        for (const std::unique_ptr<BasicBlock>& block : callee.blocks()) {
            blocks[block.get()] = caller.create_block();
        }
        BasicBlock* cont = caller.create_block();

        // Move everything after the call into the continuation block.
        std::list<std::unique_ptr<Instruction>>& site_insts = site->insts();
        auto call_it = site_insts.begin();
        while (call_it != site_insts.end() && call_it->get() != call) ++call_it;
        cont->insts().splice(cont->insts().end(), site_insts, std::next(call_it),
                             site_insts.end());
        for (const std::unique_ptr<Instruction>& moved : cont->insts()) {
            moved->set_parent(cont);
        }

        // Clone the callee body.
        for (const std::unique_ptr<BasicBlock>& block : callee.blocks()) {
            BasicBlock* target = blocks[block.get()];
            for (const std::unique_ptr<Instruction>& inst : block->insts()) {
                const Opcode op = inst->opcode();
                if (op == Opcode::Alloca) continue;  // already cloned into entry
                if (op == Opcode::Ret) {
                    Value* rv = remap(inst->operand(0), values);
                    target->push_back(std::make_unique<StoreInst>(result_slot, rv));
                    target->push_back(std::make_unique<BrInst>(cont));
                } else if (op == Opcode::Br) {
                    target->push_back(std::make_unique<BrInst>(
                        blocks[static_cast<const BasicBlock*>(inst->operand(0))]));
                } else if (op == Opcode::CondBr) {
                    target->push_back(std::make_unique<CondBrInst>(
                        remap(inst->operand(0), values),
                        blocks[static_cast<const BasicBlock*>(inst->operand(1))],
                        blocks[static_cast<const BasicBlock*>(inst->operand(2))]));
                } else {
                    std::unique_ptr<Instruction> clone = clone_value_inst(*inst, values);
                    values[inst.get()] = clone.get();
                    target->push_back(std::move(clone));
                }
            }
        }

        // Load the result at the top of the continuation and redirect uses.
        auto load = std::make_unique<LoadInst>(result_slot, module_.fresh_id());
        Value* result = load.get();
        cont->push_front(std::move(load));
        call->replace_all_uses_with(result);

        // Erase the call and wire the site into the cloned entry.
        for (unsigned k = 0; k < call->num_operands(); ++k) {
            call->operand(k)->remove_use(call);
        }
        site_insts.erase(call_it);
        site->push_back(std::make_unique<BrInst>(blocks[callee.entry()]));
    }

    Module& module_;
};

}  // namespace

bool inline_functions(Module& module) {
    Inliner inliner(module);
    return inliner.run();
}

}  // namespace toyc
