#include "toyc/const_eval.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <utility>

namespace toyc {

namespace {

bool is_binary_opcode(Opcode opcode) {
    switch (opcode) {
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Sdiv:
        case Opcode::Srem:
        case Opcode::ICmpEq:
        case Opcode::ICmpNe:
        case Opcode::ICmpSlt:
        case Opcode::ICmpSgt:
        case Opcode::ICmpSle:
        case Opcode::ICmpSge:
            return true;
        default:
            return false;
    }
}

std::uint32_t to_bits(int value) {
    return std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(value));
}

int from_bits(std::uint32_t value) {
    return static_cast<int>(std::bit_cast<std::int32_t>(value));
}

}  // namespace

bool ConstChecker::is_const_callable(const Function& function) {
    states_.clear();
    return check_function(function);
}

bool ConstChecker::check_function(const Function& function) {
    auto found = states_.find(&function);
    if (found != states_.end()) {
        return found->second != State::Rejected;
    }
    if (function.ret_type() != FuncRet::Int || !function.entry()) {
        states_[&function] = State::Rejected;
        return false;
    }

    states_[&function] = State::Checking;
    for (const std::unique_ptr<BasicBlock>& block : function.blocks()) {
        if (!block->is_terminated()) {
            states_[&function] = State::Rejected;
            return false;
        }
        for (const std::unique_ptr<Instruction>& instruction : block->insts()) {
            if (!check_instruction(*instruction)) {
                states_[&function] = State::Rejected;
                return false;
            }
        }
    }
    states_[&function] = State::ConstCallable;
    return true;
}

bool ConstChecker::check_instruction(const Instruction& instruction) {
    const Opcode opcode = instruction.opcode();
    if (is_binary_opcode(opcode)) {
        return instruction.num_operands() == 2;
    }
    switch (opcode) {
        case Opcode::Neg:
            return instruction.num_operands() == 1;
        case Opcode::Phi: {
            const auto& phi = static_cast<const PhiInst&>(instruction);
            return instruction.num_operands() != 0 &&
                   instruction.num_operands() == phi.incoming_blocks().size();
        }
        case Opcode::Shl:
            return instruction.num_operands() == 1 &&
                   static_cast<const ShlInst&>(instruction).amount() < 32;
        case Opcode::Shr:
            return instruction.num_operands() == 1 &&
                   static_cast<const ShrInst&>(instruction).amount() < 32;
        case Opcode::Br:
            return instruction.num_operands() == 1;
        case Opcode::CondBr:
            return instruction.num_operands() == 3;
        case Opcode::Ret:
            return instruction.num_operands() == 1;
        case Opcode::Call: {
            const auto& call = static_cast<const CallInst&>(instruction);
            const Function* callee = module_.find_function(call.callee_name());
            return call.has_result() && callee &&
                   call.num_operands() == callee->params().size() &&
                   check_function(*callee);
        }
        case Opcode::Alloca:
        case Opcode::Load:
        case Opcode::Store:
            return false;
        default:
            return false;
    }
}

ConstEvalResult ConstEvaluator::evaluate(const Function& function,
                                         const std::vector<int>& arguments) {
    step_count_ = 0;
    error_ = ConstEvalError::None;
    if (!checker_.is_const_callable(function)) {
        return {std::nullopt, ConstEvalError::NotConstCallable};
    }

    std::optional<int> value = evaluate_function(function, arguments, 0);
    if (!value && error_ == ConstEvalError::None) {
        error_ = ConstEvalError::InvalidIr;
    }
    return {value, error_};
}

std::optional<int> ConstEvaluator::evaluate_function(
    const Function& function, const std::vector<int>& arguments,
    std::size_t call_depth) {
    if (arguments.size() != function.params().size()) {
        fail(ConstEvalError::ArgumentCountMismatch);
        return std::nullopt;
    }
    if (call_depth >= limits_.max_call_depth) {
        fail(ConstEvalError::CallDepthExceeded);
        return std::nullopt;
    }

    std::unordered_map<const Value*, int> values;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        values.emplace(function.param(static_cast<unsigned>(index)), arguments[index]);
    }

    BasicBlock* block = function.entry();
    BasicBlock* predecessor = nullptr;
    while (block) {
        auto instruction = block->insts().begin();
        std::vector<std::pair<const Instruction*, int>> phi_values;
        while (instruction != block->insts().end() &&
               (*instruction)->opcode() == Opcode::Phi) {
            if (!take_step() || !predecessor) {
                if (!predecessor) {
                    fail(ConstEvalError::InvalidIr);
                }
                return std::nullopt;
            }
            const auto& phi = static_cast<const PhiInst&>(**instruction);
            std::size_t incoming = phi.incoming_blocks().size();
            for (std::size_t index = 0; index < phi.incoming_blocks().size(); ++index) {
                if (phi.incoming_blocks()[index] == predecessor) {
                    incoming = index;
                    break;
                }
            }
            if (incoming == phi.incoming_blocks().size()) {
                fail(ConstEvalError::InvalidIr);
                return std::nullopt;
            }
            std::optional<int> value = read_value(phi.operand(static_cast<unsigned>(incoming)), values);
            if (!value) {
                return std::nullopt;
            }
            phi_values.emplace_back(instruction->get(), *value);
            ++instruction;
        }
        for (const auto& [phi, value] : phi_values) {
            values[phi] = value;
        }

        bool transferred = false;
        for (; instruction != block->insts().end(); ++instruction) {
            const Instruction& current = **instruction;
            if (!take_step()) {
                return std::nullopt;
            }

            const Opcode opcode = current.opcode();
            if (is_binary_opcode(opcode)) {
                std::optional<int> lhs = read_value(current.operand(0), values);
                std::optional<int> rhs = read_value(current.operand(1), values);
                if (!lhs || !rhs) {
                    return std::nullopt;
                }
                std::optional<int> value = evaluate_binary(opcode, *lhs, *rhs);
                if (!value) {
                    return std::nullopt;
                }
                values[&current] = *value;
                continue;
            }

            switch (opcode) {
                case Opcode::Neg: {
                    std::optional<int> value = read_value(current.operand(0), values);
                    if (!value) {
                        return std::nullopt;
                    }
                    values[&current] = from_bits(0U - to_bits(*value));
                    break;
                }
                case Opcode::Shl: {
                    std::optional<int> value = read_value(current.operand(0), values);
                    if (!value) {
                        return std::nullopt;
                    }
                    const unsigned amount = static_cast<const ShlInst&>(current).amount();
                    values[&current] = from_bits(to_bits(*value) << amount);
                    break;
                }
                case Opcode::Shr: {
                    std::optional<int> value = read_value(current.operand(0), values);
                    if (!value) {
                        return std::nullopt;
                    }
                    const unsigned amount = static_cast<const ShrInst&>(current).amount();
                    values[&current] = static_cast<std::int32_t>(*value) >> amount;
                    break;
                }
                case Opcode::Call: {
                    const auto& call = static_cast<const CallInst&>(current);
                    const Function* callee = module_.find_function(call.callee_name());
                    if (!callee || !checker_.is_const_callable(*callee)) {
                        fail(ConstEvalError::NotConstCallable);
                        return std::nullopt;
                    }
                    std::vector<int> call_arguments;
                    call_arguments.reserve(call.num_operands());
                    for (Value* operand : call.operands()) {
                        std::optional<int> value = read_value(operand, values);
                        if (!value) {
                            return std::nullopt;
                        }
                        call_arguments.push_back(*value);
                    }
                    std::optional<int> value = evaluate_function(
                        *callee, call_arguments, call_depth + 1);
                    if (!value) {
                        return std::nullopt;
                    }
                    values[&current] = *value;
                    break;
                }
                case Opcode::Br:
                    predecessor = block;
                    block = static_cast<BasicBlock*>(current.operand(0));
                    transferred = true;
                    break;
                case Opcode::CondBr: {
                    std::optional<int> condition = read_value(current.operand(0), values);
                    if (!condition) {
                        return std::nullopt;
                    }
                    predecessor = block;
                    block = static_cast<BasicBlock*>(
                        current.operand(*condition != 0 ? 1 : 2));
                    transferred = true;
                    break;
                }
                case Opcode::Ret:
                    return read_value(current.operand(0), values);
                default:
                    fail(ConstEvalError::InvalidIr);
                    return std::nullopt;
            }
            if (transferred) {
                break;
            }
        }
        if (!transferred) {
            fail(ConstEvalError::InvalidIr);
            return std::nullopt;
        }
    }

    fail(ConstEvalError::InvalidIr);
    return std::nullopt;
}

std::optional<int> ConstEvaluator::read_value(
    const Value* value, const std::unordered_map<const Value*, int>& values) {
    if (value->value_kind() == ValueKind::Constant) {
        return static_cast<const ConstantInt*>(value)->value();
    }
    auto found = values.find(value);
    if (found == values.end()) {
        fail(ConstEvalError::InvalidIr);
        return std::nullopt;
    }
    return found->second;
}

std::optional<int> ConstEvaluator::evaluate_binary(Opcode opcode, int lhs, int rhs) {
    switch (opcode) {
        case Opcode::Add:
            return from_bits(to_bits(lhs) + to_bits(rhs));
        case Opcode::Sub:
            return from_bits(to_bits(lhs) - to_bits(rhs));
        case Opcode::Mul:
            return from_bits(to_bits(lhs) * to_bits(rhs));
        case Opcode::Sdiv:
            if (rhs == 0) {
                fail(ConstEvalError::DivisionByZero);
                return std::nullopt;
            }
            if (lhs == std::numeric_limits<std::int32_t>::min() && rhs == -1) {
                return lhs;
            }
            return lhs / rhs;
        case Opcode::Srem:
            if (rhs == 0) {
                fail(ConstEvalError::DivisionByZero);
                return std::nullopt;
            }
            if (lhs == std::numeric_limits<std::int32_t>::min() && rhs == -1) {
                return 0;
            }
            return lhs % rhs;
        case Opcode::ICmpEq:
            return lhs == rhs ? 1 : 0;
        case Opcode::ICmpNe:
            return lhs != rhs ? 1 : 0;
        case Opcode::ICmpSlt:
            return lhs < rhs ? 1 : 0;
        case Opcode::ICmpSgt:
            return lhs > rhs ? 1 : 0;
        case Opcode::ICmpSle:
            return lhs <= rhs ? 1 : 0;
        case Opcode::ICmpSge:
            return lhs >= rhs ? 1 : 0;
        default:
            fail(ConstEvalError::InvalidIr);
            return std::nullopt;
    }
}

bool ConstEvaluator::take_step() {
    if (step_count_ >= limits_.max_steps) {
        fail(ConstEvalError::StepLimitExceeded);
        return false;
    }
    ++step_count_;
    return true;
}

void ConstEvaluator::fail(ConstEvalError error) {
    if (error_ == ConstEvalError::None) {
        error_ = error;
    }
}

}  // namespace toyc
