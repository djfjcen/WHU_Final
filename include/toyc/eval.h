#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace toyc {

struct CompUnit;
struct SemaResult;

// Budget guarding the whole-program compile-time evaluator. The judge enforces a
// compile-time limit, so the wall-clock deadline is the primary guard: when the
// evaluator cannot finish quickly it must bail out and let real codegen run.
struct EvalBudget {
    std::uint64_t max_steps = 4'000'000'000;
    unsigned max_millis = 6000;  // wall-clock budget; dominant guard
    unsigned max_call_depth = 3000;
    std::size_t max_memo_entries = 4'000'000;
};

// ToyC has no input, no I/O and a single translation unit, so the value returned
// by `main()` is fully determined by the source. When possible, evaluate it at
// compile time and let the backend emit a constant-returning `main`.
//
// Returns the 32-bit `main` return value on success, or std::nullopt when the
// program cannot be evaluated within budget or hits an unsupported/undefined
// situation. Callers MUST fall back to normal codegen on std::nullopt.
std::optional<std::int32_t> evaluate_program(const CompUnit& unit,
                                             const SemaResult& sema,
                                             const EvalBudget& budget = {});

}  // namespace toyc
