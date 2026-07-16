#include "toyc/const_eval.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace toyc {
namespace {

TEST(ConstChecker, DetectsSafeAndUnsafeFunctions) {
    Module module;
    Function* safe = module.create_function("safe", FuncRet::Int, 1);
    BasicBlock* safe_entry = safe->create_block();
    auto add = std::make_unique<BinaryInst>(
        Opcode::Add, safe->param(0), module.get_constant(1), module.fresh_id());
    Instruction* add_value = add.get();
    safe_entry->push_back(std::move(add));
    safe_entry->push_back(std::make_unique<RetInst>(add_value));

    GlobalVar* global = module.create_global("state", 0, false);
    Function* unsafe = module.create_function("unsafe", FuncRet::Int, 1);
    BasicBlock* unsafe_entry = unsafe->create_block();
    unsafe_entry->push_back(
        std::make_unique<StoreInst>(global->addr, unsafe->param(0)));
    unsafe_entry->push_back(std::make_unique<RetInst>(unsafe->param(0)));

    ConstChecker checker(module);
    EXPECT_TRUE(checker.is_const_callable(*safe));
    EXPECT_FALSE(checker.is_const_callable(*unsafe));
}

TEST(ConstChecker, RejectsEveryEntryIntoUnsafeRecursiveCycle) {
    Module module;
    Function* first = module.create_function("first", FuncRet::Int, 1);
    Function* second = module.create_function("second", FuncRet::Int, 1);

    BasicBlock* first_entry = first->create_block();
    auto first_call = std::make_unique<CallInst>(
        "second", std::vector<Value*>{first->param(0)}, false,
        module.fresh_id());
    Instruction* first_value = first_call.get();
    first_entry->push_back(std::move(first_call));
    first_entry->push_back(std::make_unique<RetInst>(first_value));

    GlobalVar* global = module.create_global("state", 0, false);
    BasicBlock* second_entry = second->create_block();
    auto second_call = std::make_unique<CallInst>(
        "first", std::vector<Value*>{second->param(0)}, false,
        module.fresh_id());
    Instruction* second_value = second_call.get();
    second_entry->push_back(std::move(second_call));
    second_entry->push_back(
        std::make_unique<StoreInst>(global->addr, second->param(0)));
    second_entry->push_back(std::make_unique<RetInst>(second_value));

    ConstChecker checker(module);
    EXPECT_FALSE(checker.is_const_callable(*first));
    EXPECT_FALSE(checker.is_const_callable(*second));
}

TEST(ConstEvaluator, EvaluatesLoopPhiNodes) {
    Module module;
    Function* sum = module.create_function("sum", FuncRet::Int, 1);
    BasicBlock* entry = sum->create_block();
    BasicBlock* header = sum->create_block();
    BasicBlock* body = sum->create_block();
    BasicBlock* exit = sum->create_block();
    entry->push_back(std::make_unique<BrInst>(header));

    auto index_phi = std::make_unique<PhiInst>(module.fresh_id());
    PhiInst* index = index_phi.get();
    auto total_phi = std::make_unique<PhiInst>(module.fresh_id());
    PhiInst* total = total_phi.get();
    header->push_back(std::move(index_phi));
    header->push_back(std::move(total_phi));
    auto condition = std::make_unique<ICmpInst>(
        Opcode::ICmpSlt, index, sum->param(0), module.fresh_id());
    Instruction* condition_value = condition.get();
    header->push_back(std::move(condition));
    header->push_back(std::make_unique<CondBrInst>(condition_value, body, exit));

    auto next_total = std::make_unique<BinaryInst>(
        Opcode::Add, total, index, module.fresh_id());
    Instruction* next_total_value = next_total.get();
    auto next_index = std::make_unique<BinaryInst>(
        Opcode::Add, index, module.get_constant(1), module.fresh_id());
    Instruction* next_index_value = next_index.get();
    body->push_back(std::move(next_total));
    body->push_back(std::move(next_index));
    body->push_back(std::make_unique<BrInst>(header));
    exit->push_back(std::make_unique<RetInst>(total));

    index->add_incoming(module.get_constant(0), entry);
    index->add_incoming(next_index_value, body);
    total->add_incoming(module.get_constant(0), entry);
    total->add_incoming(next_total_value, body);

    ConstChecker checker(module);
    ConstEvaluator evaluator(module, checker);
    ConstEvalResult result = evaluator.evaluate(*sum, {4});

    ASSERT_TRUE(result);
    EXPECT_EQ(6, *result.value);
    EXPECT_EQ(ConstEvalError::None, result.error);
}

TEST(ConstEvaluator, ReportsInputDependentFailure) {
    Module module;
    Function* divide = module.create_function("divide", FuncRet::Int, 2);
    BasicBlock* entry = divide->create_block();
    auto division = std::make_unique<BinaryInst>(
        Opcode::Sdiv, divide->param(0), divide->param(1), module.fresh_id());
    Instruction* division_value = division.get();
    entry->push_back(std::move(division));
    entry->push_back(std::make_unique<RetInst>(division_value));

    ConstChecker checker(module);
    ASSERT_TRUE(checker.is_const_callable(*divide));
    ConstEvaluator evaluator(module, checker);
    ConstEvalResult result = evaluator.evaluate(*divide, {5, 0});

    EXPECT_FALSE(result);
    EXPECT_EQ(ConstEvalError::DivisionByZero, result.error);
}

TEST(ConstEvaluator, EvaluatesBoundedRecursion) {
    Module module;
    Function* factorial = module.create_function("factorial", FuncRet::Int, 1);
    BasicBlock* entry = factorial->create_block();
    BasicBlock* base = factorial->create_block();
    BasicBlock* recurse = factorial->create_block();

    auto condition = std::make_unique<ICmpInst>(
        Opcode::ICmpSle, factorial->param(0), module.get_constant(1),
        module.fresh_id());
    Instruction* condition_value = condition.get();
    entry->push_back(std::move(condition));
    entry->push_back(std::make_unique<CondBrInst>(condition_value, base, recurse));
    base->push_back(std::make_unique<RetInst>(module.get_constant(1)));

    auto previous = std::make_unique<BinaryInst>(
        Opcode::Sub, factorial->param(0), module.get_constant(1),
        module.fresh_id());
    Instruction* previous_value = previous.get();
    recurse->push_back(std::move(previous));
    auto call = std::make_unique<CallInst>(
        "factorial", std::vector<Value*>{previous_value}, false,
        module.fresh_id());
    Instruction* call_value = call.get();
    recurse->push_back(std::move(call));
    auto product = std::make_unique<BinaryInst>(
        Opcode::Mul, factorial->param(0), call_value, module.fresh_id());
    Instruction* product_value = product.get();
    recurse->push_back(std::move(product));
    recurse->push_back(std::make_unique<RetInst>(product_value));

    ConstChecker checker(module);
    ConstEvaluator evaluator(module, checker);
    ConstEvalResult result = evaluator.evaluate(*factorial, {5});

    ASSERT_TRUE(result);
    EXPECT_EQ(120, *result.value);
}

TEST(ConstEvaluator, StopsNonTerminatingEvaluation) {
    Module module;
    Function* loop = module.create_function("loop", FuncRet::Int, 0);
    BasicBlock* entry = loop->create_block();
    BasicBlock* condition = loop->create_block();
    BasicBlock* body = loop->create_block();
    BasicBlock* exit = loop->create_block();
    entry->push_back(std::make_unique<BrInst>(condition));
    condition->push_back(std::make_unique<CondBrInst>(
        module.get_constant(1), body, exit));
    body->push_back(std::make_unique<BrInst>(condition));
    exit->push_back(std::make_unique<RetInst>(module.get_constant(0)));

    ConstChecker checker(module);
    ASSERT_TRUE(checker.is_const_callable(*loop));
    ConstEvaluator evaluator(
        module, checker, ConstEvalLimits{.max_steps = 8, .max_call_depth = 8});
    ConstEvalResult result = evaluator.evaluate(*loop, {});

    EXPECT_FALSE(result);
    EXPECT_EQ(ConstEvalError::StepLimitExceeded, result.error);
}

TEST(ConstEvaluator, StopsUnboundedRecursion) {
    Module module;
    Function* recurse = module.create_function("recurse", FuncRet::Int, 1);
    BasicBlock* entry = recurse->create_block();
    auto call = std::make_unique<CallInst>(
        "recurse", std::vector<Value*>{recurse->param(0)}, false,
        module.fresh_id());
    Instruction* call_value = call.get();
    entry->push_back(std::move(call));
    entry->push_back(std::make_unique<RetInst>(call_value));

    ConstChecker checker(module);
    ASSERT_TRUE(checker.is_const_callable(*recurse));
    ConstEvaluator evaluator(
        module, checker, ConstEvalLimits{.max_steps = 100, .max_call_depth = 4});
    ConstEvalResult result = evaluator.evaluate(*recurse, {1});

    EXPECT_FALSE(result);
    EXPECT_EQ(ConstEvalError::CallDepthExceeded, result.error);
}

}  // namespace
}  // namespace toyc
