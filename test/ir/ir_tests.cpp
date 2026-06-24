#include "toyc/ir.h"
#include "toyc/ir_builder.h"
#include "toyc/ir_printer.h"
#include "toyc/mem2reg.h"
#include "toyc/optim.h"

#include "check.h"

#include <algorithm>
#include <sstream>

using namespace toyc;

namespace {

void test_use_list_wiring() {
    Module m;
    Value* a = m.create_register(Type::I32);
    Value* b = m.create_register(Type::I32);
    auto u = std::make_unique<StoreInst>(a, b);  // 2 operands: a(ptr), b(val)
    User* user = u.get();

    toyc::test::check(user->num_operands() == 2, "user has 2 operands");
    toyc::test::check(user->operand(0) == a && user->operand(1) == b, "operands stored");
    toyc::test::check(a->uses().size() == 1, "a used");
    toyc::test::check(b->uses().size() == 1, "b used");

    user->set_operand(0, b);
    toyc::test::check(a->uses().empty(), "a no longer used");
    toyc::test::check(b->uses().size() == 2, "b used twice");

    b->replace_all_uses_with(a);
    toyc::test::check(user->operand(0) == a && user->operand(1) == a, "RAUW rewires");
    toyc::test::check(b->uses().empty(), "b replaced out");
}

void test_constant_pool_and_globals() {
    Module m;
    ConstantInt* c1 = m.get_constant(42);
    ConstantInt* c2 = m.get_constant(42);
    ConstantInt* c3 = m.get_constant(7);

    toyc::test::check(c1 == c2, "constant 42 uniqued");
    toyc::test::check(c1 != c3, "constant 7 distinct");
    toyc::test::check(c1->value() == 42, "constant value 42");
    toyc::test::check_eq_str("42", c1->name(), "constant name is literal");

    GlobalVar* g = m.create_global("g_count", 0, /*is_const=*/false);
    toyc::test::check_eq_str("@g_count", g->addr->name(), "global addr name");
    toyc::test::check(g->addr->type() == Type::Ptr, "global addr is ptr");
    toyc::test::check(g->init->value() == 0, "global init value");
    toyc::test::check(!g->is_const, "global is var not const");

    Value* r = m.create_register(Type::I32);
    toyc::test::check_eq_str("%v.0", r->name(), "register name %v.0");
    toyc::test::check(r->type() == Type::I32, "register type i32");
}

void test_instruction_construction() {
    Module m;
    Value* a = m.create_register(Type::I32);
    Value* b = m.create_register(Type::I32);

    auto add = std::make_unique<BinaryInst>(Opcode::Add, a, b, m.fresh_id());
    toyc::test::check(add->opcode() == Opcode::Add, "add opcode");
    toyc::test::check(add->type() == Type::I32, "add result i32");
    toyc::test::check(add->has_result(), "add has result");
    toyc::test::check(add->operand(0) == a && add->operand(1) == b, "add operands");
    toyc::test::check(add->num_operands() == 2, "add 2 operands");
    toyc::test::check(!add->is_terminator(), "add not terminator");
    toyc::test::check(a->uses().size() == 1, "a used by add");

    auto slt = std::make_unique<ICmpInst>(Opcode::ICmpSlt, a, b, m.fresh_id());
    toyc::test::check(slt->opcode() == Opcode::ICmpSlt && slt->type() == Type::I32, "icmp i32 result");

    ConstantInt* one = m.get_constant(1);
    auto ret = std::make_unique<RetInst>(one);
    toyc::test::check(ret->is_terminator(), "ret terminator");
    toyc::test::check(!ret->has_result(), "ret has no result");
    toyc::test::check(ret->operand(0) == one, "ret operand");

    auto ret_void = std::make_unique<RetInst>(/*value=*/nullptr);
    toyc::test::check(ret_void->num_operands() == 0, "void ret 0 operands");

    auto store = std::make_unique<StoreInst>(a, b);
    toyc::test::check(store->opcode() == Opcode::Store, "store opcode");
    toyc::test::check(!store->has_result(), "store has no result");
    toyc::test::check(store->operand(0) == a && store->operand(1) == b, "store operands ptr,val");
}

void test_function_and_blocks() {
    Module m;
    Function* f = m.create_function("add", FuncRet::Int, /*params=*/2);

    toyc::test::check_eq_str("@add", f->name(), "function name");
    toyc::test::check(f->ret_type() == FuncRet::Int, "function ret int");
    toyc::test::check(f->params().size() == 2, "2 params");
    toyc::test::check_eq_str("%arg.0", f->param(0)->name(), "param 0 name");
    toyc::test::check_eq_str("%arg.1", f->param(1)->name(), "param 1 name");

    BasicBlock* entry = f->create_block();
    BasicBlock* bb1 = f->create_block();
    toyc::test::check_eq_str("entry", entry->name(), "entry label");
    toyc::test::check_eq_str("bb1", bb1->name(), "bb1 label");
    toyc::test::check(f->entry() == entry, "entry is first block");

    Value* a = f->param(0);
    Value* one = m.get_constant(1);
    auto add = std::make_unique<BinaryInst>(Opcode::Add, a, one, m.fresh_id());
    auto ret = std::make_unique<RetInst>(nullptr);
    Instruction* add_raw = add.get();
    entry->push_back(std::move(add));
    entry->push_back(std::move(ret));
    toyc::test::check(add_raw->parent() == entry, "inst parent set");
    toyc::test::check(entry->terminator()->opcode() == Opcode::Ret, "terminator is ret");
    toyc::test::check_eq_str("%v.0", add_raw->name(), "add result name %v.0");
}

void test_printer_basic() {
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
    toyc::test::check_eq_str(expected, out.str(), "module print");
}

void test_builder_matches_printer() {
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
    toyc::test::check_eq_str(expected, out.str(), "builder output matches direct construction");
}

void test_dominator_tree_straight() {
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
    toyc::test::check(dt.idom(entry) == entry, "straight: entry idom self");
    toyc::test::check(dt.idom(mid) == entry, "straight: mid idom entry");
    toyc::test::check(dt.idom(end) == mid, "straight: end idom mid");
    toyc::test::check(dt.succs(entry).size() == 1 && dt.succs(entry)[0] == mid, "straight: entry succ mid");
    toyc::test::check(dt.preds(end).size() == 1 && dt.preds(end)[0] == mid, "straight: end pred mid");
}

void test_dominator_tree_diamond() {
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
    toyc::test::check(dt.idom(then) == entry, "diamond: then idom entry");
    toyc::test::check(dt.idom(els) == entry, "diamond: els idom entry");
    toyc::test::check(dt.idom(merge) == entry, "diamond: merge idom entry");
    toyc::test::check(dt.preds(merge).size() == 2, "diamond: merge 2 preds");
    toyc::test::check(dt.children(entry).size() == 3, "diamond: entry 3 dom children");
}

void test_dominator_tree_loop() {
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
    toyc::test::check(dt.idom(header) == entry, "loop: header idom entry");
    toyc::test::check(dt.idom(body) == header, "loop: body idom header");
    toyc::test::check(dt.idom(exit) == header, "loop: exit idom header");
    toyc::test::check(dt.preds(header).size() == 2, "loop: header 2 preds (entry+body)");
}

void test_dom_frontier() {
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
    toyc::test::check(contains(dt.dom_frontier(body), header), "df: DF(body) has header");
    toyc::test::check(contains(dt.dom_frontier(header), header), "df: DF(header) has header");
    toyc::test::check(dt.dom_frontier(entry).empty(), "df: DF(entry) empty");
    toyc::test::check(dt.dom_frontier(exit).empty(), "df: DF(exit) empty");
}

void test_mem2reg_inserts_phi() {
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
    toyc::test::check(merge_has_phi, "m2r: phi inserted at merge");
    toyc::test::check(then->insts().front()->opcode() != Opcode::Phi, "m2r: no phi at then");
}

void test_mem2reg_rename() {
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
    toyc::test::check(term->opcode() == Opcode::Ret, "rename: merge ends in ret");
    toyc::test::check(term->operand(0)->value_kind() != ValueKind::Register ||
                      term->operand(0) != load_raw, "rename: ret no longer uses old load");
    // find the phi in merge
    PhiInst* phi = nullptr;
    for (const std::unique_ptr<Instruction>& inst : merge->insts()) {
        if (inst->opcode() == Opcode::Phi) { phi = static_cast<PhiInst*>(inst.get()); break; }
    }
    toyc::test::check(phi != nullptr, "rename: merge has phi");
    toyc::test::check(phi && phi->num_operands() == 2, "rename: phi has 2 incoming");
    toyc::test::check(phi && term->operand(0) == phi, "rename: ret uses phi");
}

void test_mem2reg_full_ssa() {
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
    toyc::test::check(dead == 0, "cleanup: no alloca/load/store left");

    // Phi incoming order matches merge's preds order (then, els).
    DominatorTree dt;
    dt.analyze(*f);
    PhiInst* phi = nullptr;
    for (const std::unique_ptr<Instruction>& inst : merge->insts()) {
        if (inst->opcode() == Opcode::Phi) { phi = static_cast<PhiInst*>(inst.get()); break; }
    }
    const std::vector<BasicBlock*>& preds = dt.preds(merge);
    toyc::test::check(phi && phi->num_operands() == preds.size(), "normalize: phi arity == preds");
    bool order_ok = true;
    for (unsigned i = 0; phi && i < preds.size(); ++i) {
        if (phi->incoming_blocks()[i] != preds[i]) order_ok = false;
    }
    toyc::test::check(order_ok, "normalize: phi incoming == preds order");

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
    toyc::test::check_eq_str(expected, out.str(), "full ssa print");
}

void test_constprop_folds_binary() {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    auto mul = std::make_unique<BinaryInst>(Opcode::Mul, m.get_constant(3), m.get_constant(4), m.fresh_id());
    Instruction* mul_raw = mul.get();
    entry->push_back(std::move(mul));
    entry->push_back(std::make_unique<RetInst>(mul_raw));

    bool changed = constprop(*f);
    toyc::test::check(changed, "cp: changed");
    toyc::test::check(entry->insts().size() == 1, "cp: folded mul removed");
    Instruction* term = entry->terminator();
    toyc::test::check(term->opcode() == Opcode::Ret, "cp: ret remains");
    toyc::test::check(term->operand(0)->value_kind() == ValueKind::Constant, "cp: ret operand constant");
    toyc::test::check(static_cast<ConstantInt*>(term->operand(0))->value() == 12, "cp: 3*4 == 12");
}

void test_constprop_propagates_chain() {
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
    toyc::test::check(term->opcode() == Opcode::Ret, "cp-chain: ret");
    toyc::test::check(static_cast<ConstantInt*>(term->operand(0))->value() == 14, "cp-chain: 2+3*4 == 14");
    toyc::test::check(entry->insts().size() == 1, "cp-chain: only ret left");
}

void test_constprop_skips_div_zero() {
    Module m;
    Function* f = m.create_function("f", FuncRet::Int, 0);
    BasicBlock* entry = f->create_block();
    auto div = std::make_unique<BinaryInst>(Opcode::Sdiv, m.get_constant(5), m.get_constant(0), m.fresh_id());
    Instruction* div_raw = div.get();
    entry->push_back(std::move(div));
    entry->push_back(std::make_unique<RetInst>(div_raw));

    bool changed = constprop(*f);
    toyc::test::check(!changed, "cp: div-by-zero not folded");
    toyc::test::check(entry->insts().size() == 2, "cp: div preserved");
}

}  // namespace

int main() {
    test_use_list_wiring();
    test_constant_pool_and_globals();
    test_instruction_construction();
    test_function_and_blocks();
    test_printer_basic();
    test_builder_matches_printer();
    test_dominator_tree_straight();
    test_dominator_tree_diamond();
    test_dominator_tree_loop();
    test_dom_frontier();
    test_mem2reg_inserts_phi();
    test_mem2reg_rename();
    test_mem2reg_full_ssa();
    test_constprop_folds_binary();
    test_constprop_propagates_chain();
    test_constprop_skips_div_zero();
    return toyc::test::report();
}
