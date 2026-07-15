#pragma once

#include "toyc/ir.h"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace toyc {

struct ConstEvalLimits {
    std::size_t max_steps = 10000;
    std::size_t max_call_depth = 128;
};

enum class ConstEvalError {
    None,
    NotConstCallable,
    ArgumentCountMismatch,
    DivisionByZero,
    StepLimitExceeded,
    CallDepthExceeded,
    InvalidIr,
};

struct ConstEvalResult {
    std::optional<int> value;
    ConstEvalError error = ConstEvalError::None;

    explicit operator bool() const { return value.has_value(); }
};

class ConstChecker {
public:
    explicit ConstChecker(const Module& module) : module_(module) {}

    bool is_const_callable(const Function& function);

private:
    enum class State { Checking, ConstCallable, Rejected };

    bool check_function(const Function& function);
    bool check_instruction(const Instruction& instruction);

    const Module& module_;
    std::unordered_map<const Function*, State> states_;
};

class ConstEvaluator {
public:
    ConstEvaluator(const Module& module, ConstChecker& checker,
                   ConstEvalLimits limits = {})
        : module_(module), checker_(checker), limits_(limits) {}

    ConstEvalResult evaluate(const Function& function,
                             const std::vector<int>& arguments);

private:
    std::optional<int> evaluate_function(const Function& function,
                                         const std::vector<int>& arguments,
                                         std::size_t call_depth);
    std::optional<int> read_value(
        const Value* value,
        const std::unordered_map<const Value*, int>& values);
    std::optional<int> evaluate_binary(Opcode opcode, int lhs, int rhs);
    bool take_step();
    void fail(ConstEvalError error);

    const Module& module_;
    ConstChecker& checker_;
    ConstEvalLimits limits_;
    std::size_t step_count_ = 0;
    ConstEvalError error_ = ConstEvalError::None;
};

}  // namespace toyc
