#include "toyc/mem2reg.h"

#include <algorithm>
#include <iterator>
#include <list>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace toyc {

namespace {

// Cooper-Harvey-Kennedy intersect: walk both nodes up toward the root using
// postorder numbers (larger = closer to entry).
BasicBlock* intersect(BasicBlock* b1, BasicBlock* b2,
                      const std::unordered_map<BasicBlock*, unsigned>& po_num,
                      const std::unordered_map<BasicBlock*, BasicBlock*>& idom) {
    while (b1 != b2) {
        while (po_num.at(b1) < po_num.at(b2)) b1 = idom.at(b1);
        while (po_num.at(b2) < po_num.at(b1)) b2 = idom.at(b2);
    }
    return b1;
}

}  // namespace

void DominatorTree::build_cfg(Function& fn) {
    preds_.clear();
    succs_.clear();
    for (const std::unique_ptr<BasicBlock>& bb : fn.blocks()) {
        preds_[bb.get()];
        succs_[bb.get()];
    }
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        BasicBlock* bb = owner.get();
        Instruction* term = bb->terminator();
        if (!term) continue;
        std::vector<BasicBlock*> ss;
        if (term->opcode() == Opcode::Br) {
            ss.push_back(static_cast<BasicBlock*>(term->operand(0)));
        } else if (term->opcode() == Opcode::CondBr) {
            ss.push_back(static_cast<BasicBlock*>(term->operand(1)));
            ss.push_back(static_cast<BasicBlock*>(term->operand(2)));
        }
        for (BasicBlock* s : ss) {
            succs_[bb].push_back(s);
            preds_[s].push_back(bb);
        }
    }
}

void DominatorTree::analyze(Function& fn) {
    build_cfg(fn);
    BasicBlock* entry = fn.entry();
    assert(entry != nullptr);

    // Iterative postorder DFS from entry.
    std::vector<BasicBlock*> postorder;
    std::unordered_map<BasicBlock*, unsigned> po_num;
    {
        std::vector<std::pair<BasicBlock*, unsigned>> stk;
        std::unordered_map<BasicBlock*, bool> seen;
        seen[entry] = true;
        stk.push_back({entry, 0});
        while (!stk.empty()) {
            BasicBlock* bb = stk.back().first;
            unsigned& idx = stk.back().second;
            const std::vector<BasicBlock*>& ss = succs_[bb];
            if (idx < ss.size()) {
                BasicBlock* s = ss[idx++];
                if (!seen[s]) {
                    seen[s] = true;
                    stk.push_back({s, 0});
                }
            } else {
                po_num[bb] = static_cast<unsigned>(postorder.size());
                postorder.push_back(bb);
                stk.pop_back();
            }
        }
    }

    // CHK iterative idom. Iterate in reverse postorder (skip entry).
    idom_[entry] = entry;
    for (auto it = postorder.rbegin() + 1; it != postorder.rend(); ++it) {
        idom_[*it] = nullptr;
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = postorder.rbegin() + 1; it != postorder.rend(); ++it) {
            BasicBlock* b = *it;
            BasicBlock* new_idom = nullptr;
            for (BasicBlock* p : preds_[b]) {
                if (idom_[p] == nullptr) continue;
                new_idom = (new_idom == nullptr) ? p : intersect(p, new_idom, po_num, idom_);
            }
            if (idom_[b] != new_idom) {
                idom_[b] = new_idom;
                changed = true;
            }
        }
    }

    // Dom-tree children, filled in reverse postorder for deterministic DFS.
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        children_[owner.get()];
    }
    for (auto it = postorder.rbegin(); it != postorder.rend(); ++it) {
        BasicBlock* b = *it;
        if (b != entry) {
            children_[idom_[b]].push_back(b);
        }
    }

    // Dominance frontier (runner algorithm).
    for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
        BasicBlock* b = owner.get();
        df_[b];
        if (preds_[b].size() < 2) continue;
        for (BasicBlock* p : preds_[b]) {
            BasicBlock* runner = p;
            while (runner != idom_[b]) {
                std::vector<BasicBlock*>& d = df_[runner];
                if (std::find(d.begin(), d.end(), b) == d.end()) {
                    d.push_back(b);
                }
                runner = idom_[runner];
            }
        }
    }
}

namespace {

struct Mem2RegCtx {
    Function& fn;
    Module* mod;
    DominatorTree dt;
    std::vector<AllocaInst*> promotable;
    std::unordered_map<AllocaInst*, bool> promotable_set;
    std::unordered_map<PhiInst*, AllocaInst*> phi_alloca;

    explicit Mem2RegCtx(Function& f) : fn(f), mod(f.module()) { dt.analyze(fn); }

    void run() {
        collect_promotable();
        for (AllocaInst* a : promotable) insert_phi(a);
        rename();     // Task 4
        cleanup();    // Task 5
    }

    void collect_promotable() {
        for (const std::unique_ptr<Instruction>& inst : fn.entry()->insts()) {
            if (inst->opcode() != Opcode::Alloca) continue;
            AllocaInst* a = static_cast<AllocaInst*>(inst.get());
            bool ok = true;
            for (User* u : a->uses()) {
                Opcode op = static_cast<Instruction*>(u)->opcode();
                if (op != Opcode::Load && op != Opcode::Store) { ok = false; break; }
            }
            if (ok) {
                promotable.push_back(a);
                promotable_set[a] = true;
            }
        }
    }

    void insert_phi(AllocaInst* a) {
        std::unordered_set<BasicBlock*> has_phi;
        std::vector<BasicBlock*> worklist;
        for (User* u : a->uses()) {
            Instruction* inst = static_cast<Instruction*>(u);
            if (inst->opcode() == Opcode::Store) {
                worklist.push_back(inst->parent());
            }
        }
        while (!worklist.empty()) {
            BasicBlock* x = worklist.back();
            worklist.pop_back();
            for (BasicBlock* y : dt.dom_frontier(x)) {
                if (has_phi.count(y)) continue;
                auto phi = std::make_unique<PhiInst>(mod->fresh_id());
                PhiInst* raw = phi.get();
                y->push_front(std::move(phi));
                has_phi.insert(y);
                phi_alloca[raw] = a;
                worklist.push_back(y);
            }
        }
    }

    using Stacks = std::unordered_map<AllocaInst*, std::vector<Value*>>;

    void rename() {
        Stacks stacks;
        for (AllocaInst* a : promotable) stacks[a];
        rename_block(fn.entry(), stacks);
    }

    Value* current_value(AllocaInst* a, Stacks& stacks) {
        auto found = stacks.find(a);
        if (found == stacks.end() || found->second.empty()) {
            return mod->get_constant(0);
        }
        return found->second.back();
    }

    AllocaInst* alloca_of(Value* v) const {
        if (v->value_kind() != ValueKind::Register) return nullptr;
        if (static_cast<Instruction*>(v)->opcode() != Opcode::Alloca) return nullptr;
        AllocaInst* a = static_cast<AllocaInst*>(v);
        return promotable_set.count(a) ? a : nullptr;
    }

    void rename_block(BasicBlock* bb, Stacks& stacks) {
        std::vector<AllocaInst*> pushed;

        // Phis at block front define new values for their allocas.
        for (const std::unique_ptr<Instruction>& inst : bb->insts()) {
            if (inst->opcode() != Opcode::Phi) break;
            PhiInst* phi = static_cast<PhiInst*>(inst.get());
            auto it = phi_alloca.find(phi);
            if (it != phi_alloca.end()) {
                stacks[it->second].push_back(phi);
                pushed.push_back(it->second);
            }
        }

        // Rewrite loads / record stores.
        for (const std::unique_ptr<Instruction>& inst : bb->insts()) {
            Opcode op = inst->opcode();
            if (op == Opcode::Phi) continue;
            if (op == Opcode::Load) {
                AllocaInst* a = alloca_of(inst->operand(0));
                if (a) {
                    inst->replace_all_uses_with(current_value(a, stacks));
                }
            } else if (op == Opcode::Store) {
                AllocaInst* a = alloca_of(inst->operand(0));
                if (a) {
                    stacks[a].push_back(inst->operand(1));
                    pushed.push_back(a);
                }
            }
        }

        // Fill successor phis with the current value for this block's edge.
        for (BasicBlock* s : dt.succs(bb)) {
            for (const std::unique_ptr<Instruction>& inst : s->insts()) {
                if (inst->opcode() != Opcode::Phi) break;
                PhiInst* phi = static_cast<PhiInst*>(inst.get());
                auto it = phi_alloca.find(phi);
                if (it == phi_alloca.end()) continue;
                phi->add_incoming(current_value(it->second, stacks), bb);
            }
        }

        // Recurse over dominator-tree children (RPO-ordered -> deterministic).
        for (BasicBlock* c : dt.children(bb)) {
            rename_block(c, stacks);
        }

        // Pop everything this block pushed.
        for (auto it = pushed.rbegin(); it != pushed.rend(); ++it) {
            stacks[*it].pop_back();
        }
    }

    void cleanup() {
        // Pass 1: erase loads and stores to promotable allocas.
        for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
            BasicBlock* bb = owner.get();
            std::list<std::unique_ptr<Instruction>>& insts = bb->insts();
            for (auto it = insts.begin(); it != insts.end(); ) {
                Opcode op = (*it)->opcode();
                bool del = false;
                if (op == Opcode::Load) {
                    if (alloca_of((*it)->operand(0))) del = true;
                } else if (op == Opcode::Store) {
                    if (alloca_of((*it)->operand(0))) del = true;
                }
                if (del) {
                    for (unsigned k = 0; k < (*it)->num_operands(); ++k)
                        (*it)->operand(k)->remove_use(it->get());
                    it = insts.erase(it);
                } else {
                    it = std::next(it);
                }
            }
        }
        // Pass 2: erase promotable allocas (after all their users are gone).
        for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
            BasicBlock* bb = owner.get();
            std::list<std::unique_ptr<Instruction>>& insts = bb->insts();
            for (auto it = insts.begin(); it != insts.end(); ) {
                if ((*it)->opcode() == Opcode::Alloca &&
                    promotable_set.count(static_cast<AllocaInst*>(it->get()))) {
                    for (unsigned k = 0; k < (*it)->num_operands(); ++k)
                        (*it)->operand(k)->remove_use(it->get());
                    it = insts.erase(it);
                } else {
                    it = std::next(it);
                }
            }
        }
        normalize_phis();
    }

    void normalize_phis() {
        for (const std::unique_ptr<BasicBlock>& owner : fn.blocks()) {
            BasicBlock* bb = owner.get();
            for (const std::unique_ptr<Instruction>& inst : bb->insts()) {
                if (inst->opcode() != Opcode::Phi) continue;
                PhiInst* phi = static_cast<PhiInst*>(inst.get());
                phi->reorder_incoming(dt.preds(bb));
            }
        }
    }
};

}  // namespace

void mem2reg(Function& fn) {
    if (!fn.entry()) return;
    Mem2RegCtx ctx(fn);
    ctx.run();
}

void mem2reg(Module& module) {
    for (const std::unique_ptr<Function>& fn : module.functions()) {
        mem2reg(*fn);
    }
}

}  // namespace toyc
