#include "toyc/ir.h"
#include "toyc/ir_builder.h"
#include "toyc/ir_printer.h"
#include "toyc/mem2reg.h"
#include "toyc/optim.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>

namespace toyc {
namespace {

TEST(UseList, Wiring) {
    Module m;
    Value* a = m.create_register(Type::I32);
    Value* b = m.create_register(Type::I32);
    auto u = std::make_unique<StoreInst>(a, b);  // 2 operands: a(ptr), b(val)
    User* user = u.get();

    EXPECT_EQ(2u, user->num_operands());
    EXPECT_TRUE(user->operand(0) == a && user->operand(1) == b);
    EXPECT_EQ(1u, a->uses().size());
    EXPECT_EQ(1u, b->uses().size());

    user->set_operand(0, b);
    EXPECT_TRUE(a->uses().empty());
    EXPECT_EQ(2u, b->uses().size());

    b->replace_all_uses_with(a);
    EXPECT_TRUE(user->operand(0) == a && user->operand(1) == a);
    EXPECT_TRUE(b->uses().empty());
}

TEST(Constant, PoolAndGlobals) {
    Module m;
    ConstantInt* c1 = m.get_constant(42);
    ConstantInt* c2 = m.get_constant(42);
    ConstantInt* c3 = m.get_constant(7);

    EXPECT_TRUE(c1 == c2);
    EXPECT_TRUE(c1 != c3);
    EXPECT_EQ(42, c1->value());
    EXPECT_EQ("42", c1->name());

    GlobalVar* g = m.create_global("g_count", 0, /*is_const=*/false);
    EXPECT_EQ("@g_count", g->addr->name());
    EXPECT_TRUE(g->addr->type() == Type::Ptr);
    EXPECT_EQ(0, g->init->value());
    EXPECT_FALSE(g->is_const);

    Value* r = m.create_register(Type::I32);
    EXPECT_EQ("%v.0", r->name());
    EXPECT_TRUE(r->type() == Type::I32);
}

TEST(Instruction, Construction) {
    Module m;
    Value* a = m.create_register(Type::I32);
    Value* b = m.create_register(Type::I32);

    auto add = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());
    EXPECT_TRUE(add->opcode() == Opcode::Add);
    EXPECT_TRUE(add->type() == Type::I32);
    EXPECT_TRUE(add->has_result());
    EXPECT_TRUE(add->operand(0) == a && add->operand(1) == b);
    EXPECT_EQ(2u, add->num_operands());
    EXPECT_FALSE(add->is_terminator());
    EXPECT_EQ(1u, a->uses().size());

    auto slt = std::make_unique<ICmpInst>(Opcode::ICmpSlt, a, b, m.fresh_id());
    EXPECT_TRUE(slt->opcode() == Opcode::ICmpSlt && slt->type() == Type::I32);

    ConstantInt* one = m.get_constant(1);
    auto ret = std::make_unique<RetInst>(one);
    EXPECT_TRUE(ret->is_terminator());
    EXPECT_FALSE(ret->has_result());
    EXPECT_EQ(one, ret->operand(0));

    auto ret_void = std::make_unique<RetInst>(/*value=*/nullptr);
    EXPECT_EQ(0u, ret_void->num_operands());

    auto store = std::make_unique<StoreInst>(a, b);
    EXPECT_TRUE(store->opcode() == Opcode::Store);
    EXPECT_FALSE(store->has_result());
    EXPECT_TRUE(store->operand(0) == a && store->operand(1) == b);
}

TEST(Function, Blocks) {
    Module m;
    Function* f = m.create_function("add", FuncRet::Int, /*params=*/2);

    EXPECT_EQ("@add", f->name());
    EXPECT_TRUE(f->ret_type() == FuncRet::Int);
    EXPECT_EQ(2u, f->params().size());
    EXPECT_EQ("%arg.0", f->param(0)->name());
    EXPECT_EQ("%arg.1", f->param(1)->name());

    BasicBlock* entry = f->create_block();
    BasicBlock* bb1 = f->create_block();
    EXPECT_EQ("entry", entry->name());
    EXPECT_EQ("bb1", bb1->name());
    EXPECT_EQ(entry, f->entry());

    Value* a = f->param(0);
    Value* one = m.get_constant(1);
    auto add = std::make_unique<BinaryInst>(Opcode::Add, a, one, m.fresh_id());
    auto ret = std::make_unique<RetInst>(nullptr);
    Instruction* add_raw = add.get();
    entry->push_back(std::move(add));
    entry->push_back(std::move(ret));
    EXPECT_EQ(entry, add_raw->parent());
    EXPECT_TRUE(entry->terminator()->opcode() == Opcode::Ret);
    EXPECT_EQ("%v.0", add_raw->name());
}

TEST(Printer, Basic) {
    Module m;
    m.create_global("g_count", 0, /*is_const=*/false);
    Function* f = m.create_function("f", FuncRet::Int, /*params=*/1);
    BasicBlock* entry = f->create_block();
    Value* a = f->param(0);

    auto alloca = std::make_unique<AllocaInst>(m.fresh_id());
    Value* slot = alloca.get();
    auto store = std::make_unique<StoreInst>(slot, a);
    auto load = std::make_unique<LoadInst>(slot, m.fresh_id());
    Value* loaded = load.get();
    ConstantInt* one = m.get_constant(1);
    auto add = std::make_unique<BinaryInst>(Opcode::Add, loaded, one, m.fresh_id());
    Value* addv = add.get();
    auto ret = std::make_unique<RetInst>(addv);
    entry->push_back(std::move(alloca));
    entry->push_back(std::move(store));
    entry->push_back(std::move(load));
    entry->push_back(std::move(add));
    entry->push_back(std::move(ret));

    std::ostringstream out;
    print_module(m, out);

    std::string expected =
        "@g_count = global i32 0\n"
        "\n"
        "define i32 @f(i32 %arg.0) {\n"
        "entry:\n"
        "  %v.0 = alloca i32\n"
        "  store %v.0, %arg.0\n"
        "  %v.1 = load %v.0\n"
        "  %v.2 = add %v.1, 1\n"
        "  ret %v.2\n"
        "}\n";
    EXPECT_EQ(expected, out.str());
}

TEST(Printer, BuilderMatches) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 1);
    BasicBlock* entry = f->create_block();
    IRBuilder b(m, entry);

    Value* slot = b.create_alloca();
    b.create_store(slot, f->param(0));
    Value* loaded = b.create_load(slot);
    Value* sum = b.create_binary(Opcode::Add, loaded, m.get_constant(1));
    b.create_ret(sum);

    std::ostringstream out;
    print_module(m, out);

    std::string expected =
        "define i32 @f(i32 %arg.0) {\n"
        "entry:\n"
        "  %v.0 = alloca i32\n"
        "  store %v.0, %arg.0\n"
        "  %v.1 = load %v.0\n"
        "  %v.2 = add %v.1, 1\n"
        "  ret %v.2\n"
        "}\n";
    EXPECT_EQ(expected, out.str());
}

TEST(DominatorTree, Straight) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();   // entry
    BasicBlock* mid = f->create_block();     // bb1
    BasicBlock* end = f->create_block();     // bb2
    entry->push_back(std::make_unique<BrInst>(mid));
    mid->push_back(std::make_unique<BrInst>(end));
    end->push_back(std::make_unique<RetInst>(m.get_constant(0)));

    DominatorTree dt;
    dt.analyze(*f);
    EXPECT_EQ(entry, dt.idom(entry));
    EXPECT_EQ(entry, dt.idom(mid));
    EXPECT_EQ(mid, dt.idom(end));
    EXPECT_EQ(1u, dt.succs(entry).size());
    EXPECT_EQ(mid, dt.succs(entry)[0]);
    EXPECT_EQ(1u, dt.preds(end).size());
    EXPECT_EQ(mid, dt.preds(end)[0]);
}

TEST(DominatorTree, Diamond) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    BasicBlock* then = f->create_block();   // bb1
    BasicBlock* els = f->create_block();    // bb2
    BasicBlock* merge = f->create_block();  // bb3
    Value* c = m.get_constant(1);
    entry->push_back(std::make_unique<CondBrInst>(c, then, els));
    then->push_back(std::make_unique<BrInst>(merge));
    els->push_back(std::make_unique<BrInst>(merge));
    merge->push_back(std::make_unique<RetInst>(m.get_constant(0)));

    DominatorTree dt;
    dt.analyze(*f);
    EXPECT_EQ(entry, dt.idom(then));
    EXPECT_EQ(entry, dt.idom(els));
    EXPECT_EQ(entry, dt.idom(merge));
    EXPECT_EQ(2u, dt.preds(merge).size());
    EXPECT_EQ(3u, dt.children(entry).size());
}

TEST(DominatorTree, Loop) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    BasicBlock* header = f->create_block();  // bb1
    BasicBlock* body = f->create_block();    // bb2
    BasicBlock* exit = f->create_block();    // bb3
    entry->push_back(std::make_unique<BrInst>(header));
    header->push_back(std::make_unique<CondBrInst>(m.get_constant(1), body, exit));
    body->push_back(std::make_unique<BrInst>(header));   // back edge
    exit->push_back(std::make_unique<RetInst>(m.get_constant(0)));

    DominatorTree dt;
    dt.analyze(*f);
    EXPECT_EQ(entry, dt.idom(header));
    EXPECT_EQ(header, dt.idom(body));
    EXPECT_EQ(header, dt.idom(exit));
    EXPECT_EQ(2u, dt.preds(header).size());
}

TEST(DominatorTree, Frontier) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    BasicBlock* header = f->create_block();  // bb1
    BasicBlock* body = f->create_block();    // bb2
    BasicBlock* exit = f->create_block();    // bb3
    entry->push_back(std::make_unique<BrInst>(header));
    header->push_back(std::make_unique<CondBrInst>(m.get_constant(1), body, exit));
    body->push_back(std::make_unique<BrInst>(header));
    exit->push_back(std::make_unique<RetInst>(m.get_constant(0)));

    DominatorTree dt;
    dt.analyze(*f);
    auto contains = [](const std::vector<BasicBlock*>& v, BasicBlock* b) {
        return std::find(v.begin(), v.end(), b) != v.end();
    };
    EXPECT_TRUE(contains(dt.dom_frontier(body), header));
    EXPECT_TRUE(contains(dt.dom_frontier(header), header));
    EXPECT_TRUE(dt.dom_frontier(entry).empty());
    EXPECT_TRUE(dt.dom_frontier(exit).empty());
}

TEST(Mem2Reg, InsertsPhi) {
    // int a; a=0; if(1){a=1;}else{a=2;} return a;  -> phi at merge.
    Module m;
    Function* f = m.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    BasicBlock* then = f->create_block();
    BasicBlock* els = f->create_block();
    BasicBlock* merge = f->create_block();

    auto alloca = std::make_unique<AllocaInst>(m.fresh_id());   // %v.0
    AllocaInst* a = alloca.get();
    entry->push_back(std::move(alloca));
    entry->push_back(std::make_unique<StoreInst>(a, m.get_constant(0)));
    entry->push_back(std::make_unique<CondBrInst>(m.get_constant(1), then, els));
    then->push_back(std::make_unique<StoreInst>(a, m.get_constant(1)));
    then->push_back(std::make_unique<BrInst>(merge));
    els->push_back(std::make_unique<StoreInst>(a, m.get_constant(2)));
    els->push_back(std::make_unique<BrInst>(merge));
    merge->push_back(std::make_unique<LoadInst>(a, m.fresh_id()));
    merge->push_back(std::make_unique<RetInst>(nullptr));

    mem2reg(*f);
    bool merge_has_phi = false;
    for (const std::unique_ptr<Instruction>& inst : merge->insts()) {
        if (inst->opcode() == Opcode::Phi) { merge_has_phi = true; break; }
    }
    EXPECT_TRUE(merge_has_phi);
    EXPECT_TRUE(then->insts().front()->opcode() != Opcode::Phi);
}

TEST(Mem2Reg, Rename) {
    Module m;
    Function* f = m.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    BasicBlock* then = f->create_block();
    BasicBlock* els = f->create_block();
    BasicBlock* merge = f->create_block();

    auto alloca = std::make_unique<AllocaInst>(m.fresh_id());   // %v.0
    AllocaInst* a = alloca.get();
    entry->push_back(std::move(alloca));
    entry->push_back(std::make_unique<StoreInst>(a, m.get_constant(0)));
    entry->push_back(std::make_unique<CondBrInst>(m.get_constant(1), then, els));
    then->push_back(std::make_unique<StoreInst>(a, m.get_constant(1)));
    then->push_back(std::make_unique<BrInst>(merge));
    els->push_back(std::make_unique<StoreInst>(a, m.get_constant(2)));
    els->push_back(std::make_unique<BrInst>(merge));
    auto load = std::make_unique<LoadInst>(a, m.fresh_id());    // %v.1
    LoadInst* load_raw = load.get();
    merge->push_back(std::move(load));
    merge->push_back(std::make_unique<RetInst>(load_raw));

    mem2reg(*f);

    // ret operand should now be the phi (not the deleted load).
    Instruction* term = merge->terminator();
    EXPECT_TRUE(term->opcode() == Opcode::Ret);
    EXPECT_TRUE(term->operand(0)->value_kind() != ValueKind::Register ||
                term->operand(0) != load_raw);
    PhiInst* phi = nullptr;
    for (const std::unique_ptr<Instruction>& inst : merge->insts()) {
        if (inst->opcode() == Opcode::Phi) { phi = static_cast<PhiInst*>(inst.get()); break; }
    }
    EXPECT_NE(nullptr, phi);
    EXPECT_TRUE(phi && phi->num_operands() == 2);
    EXPECT_TRUE(phi && term->operand(0) == phi);
}

TEST(Mem2Reg, FullSsa) {
    Module m;
    Function* f = m.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    BasicBlock* then = f->create_block();
    BasicBlock* els = f->create_block();
    BasicBlock* merge = f->create_block();

    auto alloca = std::make_unique<AllocaInst>(m.fresh_id());   // %v.0
    AllocaInst* a = alloca.get();
    entry->push_back(std::move(alloca));
    entry->push_back(std::make_unique<StoreInst>(a, m.get_constant(0)));
    entry->push_back(std::make_unique<CondBrInst>(m.get_constant(1), then, els));
    then->push_back(std::make_unique<StoreInst>(a, m.get_constant(1)));
    then->push_back(std::make_unique<BrInst>(merge));
    els->push_back(std::make_unique<StoreInst>(a, m.get_constant(2)));
    els->push_back(std::make_unique<BrInst>(merge));
    merge->push_back(std::make_unique<LoadInst>(a, m.fresh_id()));  // %v.1
    LoadInst* load_raw = static_cast<LoadInst*>(merge->insts().back().get());
    merge->push_back(std::make_unique<RetInst>(load_raw));

    mem2reg(*f);

    // No alloca / load / store to the promoted slot anywhere.
    int dead = 0;
    for (const std::unique_ptr<BasicBlock>& bb : f->blocks()) {
        for (const std::unique_ptr<Instruction>& inst : bb->insts()) {
            Opcode op = inst->opcode();
            if (op == Opcode::Alloca || op == Opcode::Load || op == Opcode::Store) ++dead;
        }
    }
    EXPECT_EQ(0, dead);

    // Phi incoming order matches merge's preds order (then, els).
    DominatorTree dt;
    dt.analyze(*f);
    PhiInst* phi = nullptr;
    for (const std::unique_ptr<Instruction>& inst : merge->insts()) {
        if (inst->opcode() == Opcode::Phi) { phi = static_cast<PhiInst*>(inst.get()); break; }
    }
    const std::vector<BasicBlock*>& preds = dt.preds(merge);
    EXPECT_TRUE(phi && phi->num_operands() == preds.size());
    bool order_ok = true;
    for (unsigned i = 0; phi && i < preds.size(); ++i) {
        if (phi->incoming_blocks()[i] != preds[i]) order_ok = false;
    }
    EXPECT_TRUE(order_ok);

    std::ostringstream out;
    print_module(m, out);
    std::string expected =
        "define i32 @main() {\n"
        "entry:\n"
        "  cond_br 1, label bb1, label bb2\n"
        "bb1:\n"
        "  br label bb3\n"
        "bb2:\n"
        "  br label bb3\n"
        "bb3:\n"
        "  %v.2 = phi [1, bb1], [2, bb2]\n"
        "  ret %v.2\n"
        "}\n";
    EXPECT_EQ(expected, out.str());
}

TEST(Mem2Reg, CleanupUseLists) {
    // Regression: mem2reg erase must detach operand use-lists before destroying
    // the instruction, otherwise surviving values hold dangling User* pointers.
    Module m;
    Function* f = m.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();

    auto alloca = std::make_unique<AllocaInst>(m.fresh_id());
    AllocaInst* slot = alloca.get();
    entry->push_back(std::move(alloca));
    entry->push_back(std::make_unique<StoreInst>(slot, m.get_constant(0)));

    auto x_inst = std::make_unique<BinaryInst>(Opcode::Add, m.get_constant(1), m.get_constant(2), m.fresh_id());
    Value* x = x_inst.get();
    entry->push_back(std::move(x_inst));
    entry->push_back(std::make_unique<StoreInst>(slot, x));  // erased by mem2reg

    entry->push_back(std::make_unique<RetInst>(m.get_constant(0)));

    mem2reg(*f);

    // Iterating uses of the surviving value %x must not crash (would UAF without the fix).
    for ([[maybe_unused]] User* u : x->uses()) {
    }

    Value* c = m.get_constant(99);
    x->replace_all_uses_with(c);

    EXPECT_TRUE(x->uses().empty());
}

TEST(ConstProp, FoldsBinary) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    auto mul = std::make_unique<BinaryInst>(Opcode::Mul, m.get_constant(3), m.get_constant(4), m.fresh_id());
    Instruction* mul_raw = mul.get();
    entry->push_back(std::move(mul));
    entry->push_back(std::make_unique<RetInst>(mul_raw));

    bool changed = constprop(*f);
    EXPECT_TRUE(changed);
    EXPECT_EQ(1u, entry->insts().size());
    Instruction* term = entry->terminator();
    EXPECT_TRUE(term->opcode() == Opcode::Ret);
    EXPECT_TRUE(term->operand(0)->value_kind() == ValueKind::Constant);
    EXPECT_EQ(12, static_cast<ConstantInt*>(term->operand(0))->value());
}

TEST(ConstProp, PropagatesChain) {
    // mul 3,4 -> 12 exposes add 2,12 -> 14 in one sweep-to-fixpoint.
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    auto mul = std::make_unique<BinaryInst>(Opcode::Mul, m.get_constant(3), m.get_constant(4), m.fresh_id());
    Instruction* mul_raw = mul.get();
    auto add = std::make_unique<BinaryInst>(Opcode::Add, m.get_constant(2), mul_raw, m.fresh_id());
    Instruction* add_raw = add.get();
    entry->push_back(std::move(mul));
    entry->push_back(std::move(add));
    entry->push_back(std::make_unique<RetInst>(add_raw));

    constprop(*f);
    Instruction* term = entry->terminator();
    EXPECT_TRUE(term->opcode() == Opcode::Ret);
    EXPECT_EQ(14, static_cast<ConstantInt*>(term->operand(0))->value());
    EXPECT_EQ(1u, entry->insts().size());
}

TEST(ConstProp, SkipsDivZero) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    auto div = std::make_unique<BinaryInst>(Opcode::Sdiv, m.get_constant(5), m.get_constant(0), m.fresh_id());
    Instruction* div_raw = div.get();
    entry->push_back(std::move(div));
    entry->push_back(std::make_unique<RetInst>(div_raw));

    bool changed = constprop(*f);
    EXPECT_FALSE(changed);
    EXPECT_EQ(2u, entry->insts().size());
}

TEST(ConstProp, KeepsCall) {
    // An int-returning call with two constant operands must NOT be folded: it
    // reaches the binary-fold branch by operand count, but Call is not a binary
    // arithmetic/compare op. Guards the explicit Call exclusion in constprop.
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    auto call = std::make_unique<CallInst>(
        "g", std::vector<Value*>{m.get_constant(2), m.get_constant(3)}, false, m.fresh_id());
    Instruction* call_raw = call.get();
    entry->push_back(std::move(call));
    entry->push_back(std::make_unique<RetInst>(call_raw));

    bool changed = constprop(*f);
    EXPECT_FALSE(changed);
    EXPECT_EQ(2u, entry->insts().size());
    EXPECT_TRUE(entry->insts().front()->opcode() == Opcode::Call);
}

TEST(Dce, RemovesDeadCompute) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    Value* a = m.create_register(Type::I32);
    Value* b = m.create_register(Type::I32);
    entry->push_back(std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id()));  // dead
    entry->push_back(std::make_unique<RetInst>(m.get_constant(0)));

    bool changed = dce(*f);
    EXPECT_TRUE(changed);
    EXPECT_EQ(1u, entry->insts().size());
    EXPECT_TRUE(entry->terminator()->opcode() == Opcode::Ret);
}

TEST(Dce, KeepsLiveChain) {
    // add feeds ret -> live; unused mul -> dead.
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    Value* a = m.create_register(Type::I32);
    Value* b = m.create_register(Type::I32);
    auto add = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());
    Instruction* add_raw = add.get();
    entry->push_back(std::move(add));
    entry->push_back(std::make_unique<BinaryInst>(Opcode::Mul, a, b, m.fresh_id()));  // dead
    entry->push_back(std::make_unique<RetInst>(add_raw));

    dce(*f);
    EXPECT_EQ(2u, entry->insts().size());
}

TEST(Gvn, CseIdentical) {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 2);
    BasicBlock* entry = f->create_block();
    Value* a = f->param(0);
    Value* b = f->param(1);
    auto add1 = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());
    Instruction* a1 = add1.get();
    auto add2 = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());  // dup
    Instruction* a2 = add2.get();
    auto sum = std::make_unique<BinaryInst>(Opcode::Add, a1, a2, m.fresh_id());
    Instruction* sum_raw = sum.get();
    entry->push_back(std::move(add1));
    entry->push_back(std::move(add2));
    entry->push_back(std::move(sum));
    entry->push_back(std::make_unique<RetInst>(sum_raw));

    bool changed = gvn(*f);
    EXPECT_TRUE(changed);
    bool a2_present = false;
    for (const std::unique_ptr<Instruction>& inst : entry->insts()) if (inst.get() == a2) a2_present = true;
    EXPECT_FALSE(a2_present);
    EXPECT_TRUE(sum_raw->operand(0) == a1 && sum_raw->operand(1) == a1);
}

TEST(Gvn, Commutative) {
    // add(a,b) and add(b,a) must CSE together via operand normalization.
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 2);
    BasicBlock* entry = f->create_block();
    Value* a = f->param(0);
    Value* b = f->param(1);
    auto add1 = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());
    Instruction* a1 = add1.get();
    auto add2 = std::make_unique<BinaryInst>(Opcode::Add, b, a, m.fresh_id());  // commutative dup
    Instruction* a2 = add2.get();
    auto sum = std::make_unique<BinaryInst>(Opcode::Add, a1, a2, m.fresh_id());
    entry->push_back(std::move(add1));
    entry->push_back(std::move(add2));
    entry->push_back(std::move(sum));
    entry->push_back(std::make_unique<RetInst>(sum.get()));

    gvn(*f);
    bool a2_present = false;
    for (const std::unique_ptr<Instruction>& inst : entry->insts()) if (inst.get() == a2) a2_present = true;
    EXPECT_FALSE(a2_present);
}

TEST(Gvn, DominanceScoped) {
    // add(a,b) in a non-dominating sibling block must NOT be reused.
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 2);
    BasicBlock* entry = f->create_block();
    BasicBlock* left = f->create_block();
    BasicBlock* right = f->create_block();
    Value* a = f->param(0);
    Value* b = f->param(1);
    entry->push_back(std::make_unique<CondBrInst>(a, left, right));
    auto l_add = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());
    left->push_back(std::move(l_add));
    left->push_back(std::make_unique<RetInst>(a));
    auto r_add = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());
    Instruction* r_add_raw = r_add.get();
    right->push_back(std::move(r_add));
    right->push_back(std::make_unique<RetInst>(r_add_raw));

    gvn(*f);
    // right's add survives: left does not dominate right.
    bool right_add_present = false;
    for (const std::unique_ptr<Instruction>& inst : right->insts()) if (inst.get() == r_add_raw) right_add_present = true;
    EXPECT_TRUE(right_add_present);
}

TEST(Cfs, FoldsConstBranch) {
    // cond_br 1 -> br then; else unreachable -> removed; then merged into entry.
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    BasicBlock* thenB = f->create_block();
    BasicBlock* elseB = f->create_block();
    entry->push_back(std::make_unique<CondBrInst>(m.get_constant(1), thenB, elseB));
    thenB->push_back(std::make_unique<RetInst>(m.get_constant(7)));
    elseB->push_back(std::make_unique<RetInst>(m.get_constant(8)));

    bool changed = cfs(*f);
    EXPECT_TRUE(changed);
    bool else_present = false;
    for (const std::unique_ptr<BasicBlock>& b : f->blocks()) if (b.get() == elseB) else_present = true;
    EXPECT_FALSE(else_present);
    Instruction* term = f->entry()->terminator();
    EXPECT_TRUE(term->opcode() == Opcode::Ret);
    EXPECT_EQ(7, static_cast<ConstantInt*>(term->operand(0))->value());
}

TEST(Cfs, TrivialPhi) {
    // Two preds, phi with identical incomings -> replaced by that value.
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 1);
    BasicBlock* entry = f->create_block();
    BasicBlock* left = f->create_block();
    BasicBlock* right = f->create_block();
    BasicBlock* merge = f->create_block();
    Value* c = f->param(0);
    entry->push_back(std::make_unique<CondBrInst>(c, left, right));
    left->push_back(std::make_unique<BrInst>(merge));
    right->push_back(std::make_unique<BrInst>(merge));
    auto phi = std::make_unique<PhiInst>(m.fresh_id());
    PhiInst* phi_raw = phi.get();
    phi->add_incoming(m.get_constant(5), left);
    phi->add_incoming(m.get_constant(5), right);
    merge->push_back(std::move(phi));
    merge->push_back(std::make_unique<RetInst>(phi_raw));

    cfs(*f);
    Instruction* term = merge->terminator();
    EXPECT_TRUE(term->opcode() == Opcode::Ret);
    EXPECT_EQ(5, static_cast<ConstantInt*>(term->operand(0))->value());
    bool phi_present = false;
    for (const std::unique_ptr<Instruction>& inst : merge->insts()) if (inst.get() == phi_raw) phi_present = true;
    EXPECT_FALSE(phi_present);
}

TEST(RunOptim, Fixpoint) {
    // mul 3,4 -> 12 (ConstProp) exposes add 2,12 -> 14 (ConstProp next round).
    Module m;
    Function* f = m.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    auto mul = std::make_unique<BinaryInst>(Opcode::Mul, m.get_constant(3), m.get_constant(4), m.fresh_id());
    Instruction* mul_raw = mul.get();
    auto add = std::make_unique<BinaryInst>(Opcode::Add, m.get_constant(2), mul_raw, m.fresh_id());
    Instruction* add_raw = add.get();
    entry->push_back(std::move(mul));
    entry->push_back(std::move(add));
    entry->push_back(std::make_unique<RetInst>(add_raw));

    bool changed = run_optim(m);
    EXPECT_TRUE(changed);
    Instruction* term = f->entry()->terminator();
    EXPECT_TRUE(term->opcode() == Opcode::Ret);
    EXPECT_EQ(14, static_cast<ConstantInt*>(term->operand(0))->value());
    EXPECT_EQ(1u, entry->insts().size());
}

}  // namespace
}  // namespace toyc
