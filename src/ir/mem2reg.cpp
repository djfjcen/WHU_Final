#include "toyc/mem2reg.h"

#include <algorithm>
#include <cassert>
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

}  // namespace toyc
