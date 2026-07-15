#include "toyc/codegen.h"

#include "toyc/diagnostics.h"
#include "toyc/ir.h"
#include "toyc/riscv.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace toyc {

namespace {

const Function *find_main(const Module &module) {
  for (const std::unique_ptr<Function> &function : module.functions()) {
    if (function->short_name() == "main") {
      return function.get();
    }
  }
  return nullptr;
}

std::string offset_addr(int offset, RvReg base) {
  return std::to_string(offset) + "(" + reg_name(base) + ")";
}

bool is_result_slot_inst(const Instruction &inst) {
  return inst.has_result() && inst.opcode() != Opcode::Alloca;
}

bool is_direct_branch_condition_value(const Instruction &inst) {
  return inst.parent() && inst.uses().size() == 1 &&
         dynamic_cast<const Instruction *>(inst.uses().front()) &&
         static_cast<const Instruction *>(inst.uses().front())->parent() ==
             inst.parent() &&
         static_cast<const Instruction *>(inst.uses().front())->opcode() ==
             Opcode::CondBr;
}

bool positive_power_of_two_shift(int value, unsigned &shift) {
  if (value <= 1) return false;
  const std::uint32_t bits = static_cast<std::uint32_t>(value);
  if ((bits & (bits - 1u)) != 0) return false;
  shift = 0;
  for (std::uint32_t cursor = bits; cursor > 1u; cursor >>= 1u) ++shift;
  return true;
}

class FunctionFrame {
public:
  using RegisterAssignments = std::unordered_map<const Value *, RvReg>;

  explicit FunctionFrame(const Function &function,
                         const RegisterAssignments *allocations = nullptr) {
    for (const std::unique_ptr<Value> &param : function.params()) {
      if (param->id() >= 8) {
        next_offset_ += 4;
      }
    }
    outgoing_arg_size_ = next_offset_;
    for (const std::unique_ptr<BasicBlock> &block : function.blocks()) {
      int block_phi_count = 0;
      bool phi_temps_needed = false;
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (inst->opcode() == Opcode::Phi) {
          ++block_phi_count;
          if (allocations) {
            auto destination = allocations->find(inst.get());
            if (destination == allocations->end()) {
              phi_temps_needed = true;
            }
          }
        }
        if (inst->opcode() == Opcode::Call) {
          has_call_ = true;
          const unsigned stack_args =
              inst->num_operands() > 8 ? inst->num_operands() - 8 : 0;
          const int bytes = static_cast<int>(stack_args * 4);
          if (bytes > outgoing_arg_size_) {
            outgoing_arg_size_ = bytes;
          }
        }
      }
      if (!allocations || phi_temps_needed) {
        max_phi_count_ = std::max(max_phi_count_, block_phi_count);
      }
    }
    next_offset_ = outgoing_arg_size_;
    if (has_call_) {
      ra_offset_ = next_offset_;
      next_offset_ += 4;
    }
    for (int i = 0; i < max_phi_count_; ++i) {
      phi_temp_offsets_.push_back(next_offset_);
      next_offset_ += 4;
    }
    const unsigned register_params =
        static_cast<unsigned>(std::min<std::size_t>(function.params().size(), 8));
    param_offsets_.assign(register_params, -1);
    for (unsigned i = 0; i < register_params; ++i) {
      if (!allocations || !allocations->count(function.param(i))) {
        param_offsets_[i] = next_offset_;
        next_offset_ += 4;
      }
    }
    for (const std::unique_ptr<BasicBlock> &block : function.blocks()) {
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        const bool allocated = allocations && allocations->count(inst.get());
        const bool direct_condition =
            allocations && is_direct_branch_condition_value(*inst);
        if (inst->opcode() == Opcode::Alloca ||
            (is_result_slot_inst(*inst) && !allocated && !direct_condition)) {
          slots_.emplace(inst.get(), next_offset_);
          next_offset_ += 4;
        }
      }
    }
    frame_size_ = align_to(next_offset_, 16);
  }

  int frame_size() const { return frame_size_; }
  bool saves_ra() const { return ra_offset_ >= 0; }
  int ra_offset() const { return ra_offset_; }
  int stack_param_offset(unsigned id) const {
    return frame_size_ + static_cast<int>((id - 8) * 4);
  }
  bool has_register_param_slot(unsigned id) const {
    return id < param_offsets_.size() && param_offsets_[id] >= 0;
  }
  int register_param_offset(unsigned id) const { return param_offsets_[id]; }
  int phi_temp_offset(unsigned index) const { return phi_temp_offsets_[index]; }

  void reserve_callee_saved(const std::vector<RvReg> &regs) {
    for (RvReg reg : regs) {
      callee_saved_offsets_.emplace(reg, next_offset_);
      next_offset_ += 4;
    }
    frame_size_ = align_to(next_offset_, 16);
  }

  int callee_saved_offset(RvReg reg) const {
    auto found = callee_saved_offsets_.find(reg);
    return found == callee_saved_offsets_.end() ? -1 : found->second;
  }

  bool has_slot(const Value *value) const {
    return slots_.find(value) != slots_.end();
  }

  int slot_offset(const Value *value) const {
    auto found = slots_.find(value);
    return found == slots_.end() ? -1 : found->second;
  }

private:
  std::unordered_map<const Value *, int> slots_;
  std::unordered_map<RvReg, int> callee_saved_offsets_;
  std::vector<int> phi_temp_offsets_;
  std::vector<int> param_offsets_;
  int next_offset_ = 0;
  int outgoing_arg_size_ = 0;
  int ra_offset_ = -1;
  int frame_size_ = 0;
  int max_phi_count_ = 0;
  bool has_call_ = false;
};

class FunctionLowerer {
public:
  FunctionLowerer(const Function &function, const CodegenOptions &options,
                  DiagnosticEngine &diagnostics, AsmWriter &writer)
      : function_(function), options_(options), diagnostics_(diagnostics),
        writer_(writer), frame_(function) {
    plan_block_layout();
    if (options_.opt_mode) {
      plan_global_registers();
      plan_local_registers();
      plan_loop_constants();
      collect_used_callee_saved();
      frame_ = FunctionFrame(function_, &value_regs_);
      frame_.reserve_callee_saved(used_callee_saved_);
    }
  }

  bool lower() {
    if (!validate_function_shape()) {
      return false;
    }
    emit_prologue();
    for (const BasicBlock *block : layout_) {
      if (block != function_.entry()) {
        writer_.label(block_label(function_, *block));
      }
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (!lower_inst(*inst)) {
          return false;
        }
      }
    }
    writer_.label(exit_label());
    emit_epilogue();
    return true;
  }

private:
  bool validate_function_shape() {
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      if (!block->is_terminated()) {
        if (function_.ret_type() == FuncRet::Void) {
          continue;
        }
        return fail("codegen requires every basic block to be terminated");
      }
    }
    return true;
  }

  bool lower_inst(const Instruction &inst) {
    switch (inst.opcode()) {
    case Opcode::Alloca:
      return true;
    case Opcode::Load:
      return lower_load(inst);
    case Opcode::Store:
      return lower_store(inst);
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::Sdiv:
    case Opcode::Srem:
    case Opcode::Shl:
    case Opcode::Shr:
      return lower_binary(inst);
    case Opcode::Neg:
      return lower_neg(inst);
    case Opcode::ICmpEq:
    case Opcode::ICmpNe:
    case Opcode::ICmpSlt:
    case Opcode::ICmpSgt:
    case Opcode::ICmpSle:
    case Opcode::ICmpSge:
      return lower_icmp(inst);
    case Opcode::Br:
      return lower_br(inst);
    case Opcode::CondBr:
      return lower_cond_br(inst);
    case Opcode::Call:
      return lower_call(inst);
    case Opcode::Ret:
      return lower_ret(inst);
    case Opcode::Phi:
      return true;
    }
    return fail("codegen found an unknown instruction");
  }

  bool lower_load(const Instruction &inst) {
    if (inst.num_operands() != 1) {
      return fail("malformed load instruction");
    }
    if (!load_address(inst.operand(0), RvReg::T0)) {
      return false;
    }
    const RvReg destination = result_register(inst, RvReg::T1);
    writer_.inst("lw", reg_name(destination), offset_addr(0, RvReg::T0));
    return spill_result(inst, destination);
  }

  bool lower_store(const Instruction &inst) {
    if (inst.num_operands() != 2) {
      return fail("malformed store instruction");
    }
    if (!load_i32(inst.operand(1), RvReg::T1)) {
      return false;
    }
    if (!store_i32(RvReg::T1, inst.operand(0))) {
      return false;
    }
    return true;
  }

  bool lower_binary(const Instruction &inst) {
    if (inst.opcode() == Opcode::Shl || inst.opcode() == Opcode::Shr) {
      if (inst.num_operands() != 1) {
        return fail("malformed shift instruction");
      }
      RvReg source = RvReg::T0;
      if (!select_i32_register(inst.operand(0), RvReg::T0, source)) {
        return false;
      }
      const RvReg destination = result_register(inst, RvReg::T2);
      const unsigned amount = inst.opcode() == Opcode::Shl
          ? static_cast<const ShlInst &>(inst).amount()
          : static_cast<const ShrInst &>(inst).amount();
      writer_.inst(inst.opcode() == Opcode::Shl ? "slli" : "srai",
                   reg_name(destination), reg_name(source),
                   std::to_string(amount));
      return spill_result(inst, destination);
    }
    if (inst.num_operands() != 2) {
      return fail("malformed binary instruction");
    }
    if ((inst.opcode() == Opcode::Sdiv || inst.opcode() == Opcode::Srem) &&
        inst.operand(1)->value_kind() == ValueKind::Constant) {
      unsigned shift = 0;
      const int divisor =
          static_cast<const ConstantInt *>(inst.operand(1))->value();
      if (positive_power_of_two_shift(divisor, shift)) {
        RvReg source = RvReg::T0;
        if (!select_i32_register(inst.operand(0), RvReg::T0, source)) {
          return false;
        }
        const RvReg destination = result_register(inst, RvReg::T2);
        writer_.inst("srai", reg_name(RvReg::T1), reg_name(source), "31");
        writer_.inst("srli", reg_name(RvReg::T1), reg_name(RvReg::T1),
                     std::to_string(32u - shift));
        writer_.inst("add", reg_name(RvReg::T1), reg_name(source),
                     reg_name(RvReg::T1));
        writer_.inst("srai", reg_name(RvReg::T1), reg_name(RvReg::T1),
                     std::to_string(shift));
        if (inst.opcode() == Opcode::Sdiv) {
          emit_reg_copy(destination, RvReg::T1);
        } else {
          writer_.inst("slli", reg_name(RvReg::T1), reg_name(RvReg::T1),
                       std::to_string(shift));
          writer_.inst("sub", reg_name(destination), reg_name(source),
                       reg_name(RvReg::T1));
        }
        return spill_result(inst, destination);
      }
    }
    if (try_lower_immediate_binary(inst)) {
      return true;
    }
    RvReg lhs = RvReg::T0;
    RvReg rhs = RvReg::T1;
    if (!select_i32_register(inst.operand(0), RvReg::T0, lhs) ||
        !select_i32_register(inst.operand(1), RvReg::T1, rhs)) {
      return false;
    }
    const RvReg destination = result_register(inst, RvReg::T2);
    if (inst.opcode() == Opcode::Add) {
      writer_.inst("add", reg_name(destination), reg_name(lhs), reg_name(rhs));
    } else if (inst.opcode() == Opcode::Sub) {
      writer_.inst("sub", reg_name(destination), reg_name(lhs), reg_name(rhs));
    } else if (inst.opcode() == Opcode::Mul) {
      writer_.inst("mul", reg_name(destination), reg_name(lhs), reg_name(rhs));
    } else if (inst.opcode() == Opcode::Sdiv) {
      writer_.inst("div", reg_name(destination), reg_name(lhs), reg_name(rhs));
    } else if (inst.opcode() == Opcode::Srem) {
      writer_.inst("rem", reg_name(destination), reg_name(lhs), reg_name(rhs));
    }
    return spill_result(inst, destination);
  }

  bool lower_neg(const Instruction &inst) {
    if (inst.num_operands() != 1) {
      return fail("malformed neg instruction");
    }
    RvReg operand = RvReg::T0;
    if (!select_i32_register(inst.operand(0), RvReg::T0, operand)) {
      return false;
    }
    const RvReg destination = result_register(inst, RvReg::T2);
    writer_.inst("sub", reg_name(destination), reg_name(RvReg::Zero),
                 reg_name(operand));
    return spill_result(inst, destination);
  }

  bool lower_icmp(const Instruction &inst) {
    if (inst.num_operands() != 2) {
      return fail("malformed icmp instruction");
    }
    if (is_direct_branch_condition(inst)) {
      return true;
    }
    RvReg lhs = RvReg::T0;
    RvReg rhs = RvReg::T1;
    if (!select_i32_register(inst.operand(0), RvReg::T0, lhs) ||
        !select_i32_register(inst.operand(1), RvReg::T1, rhs)) {
      return false;
    }
    const RvReg destination = result_register(inst, RvReg::T2);
    switch (inst.opcode()) {
    case Opcode::ICmpSlt:
      writer_.inst("slt", reg_name(destination), reg_name(lhs), reg_name(rhs));
      break;
    case Opcode::ICmpSgt:
      writer_.inst("slt", reg_name(destination), reg_name(rhs), reg_name(lhs));
      break;
    case Opcode::ICmpSle:
      writer_.inst("slt", reg_name(destination), reg_name(rhs), reg_name(lhs));
      writer_.inst("xori", reg_name(destination), reg_name(destination), "1");
      break;
    case Opcode::ICmpSge:
      writer_.inst("slt", reg_name(destination), reg_name(lhs), reg_name(rhs));
      writer_.inst("xori", reg_name(destination), reg_name(destination), "1");
      break;
    case Opcode::ICmpEq:
      writer_.inst("xor", reg_name(destination), reg_name(lhs), reg_name(rhs));
      writer_.inst("sltiu", reg_name(destination), reg_name(destination), "1");
      break;
    case Opcode::ICmpNe:
      writer_.inst("xor", reg_name(destination), reg_name(lhs), reg_name(rhs));
      writer_.inst("sltu", reg_name(destination), reg_name(RvReg::Zero),
                   reg_name(destination));
      break;
    default:
      return fail("malformed icmp opcode");
    }
    return spill_result(inst, destination);
  }

  bool lower_br(const Instruction &inst) {
    if (inst.num_operands() != 1 ||
        inst.operand(0)->value_kind() != ValueKind::BasicBlock) {
      return fail("malformed br instruction");
    }
    const BasicBlock &target =
        *static_cast<const BasicBlock *>(inst.operand(0));
    if (!emit_phi_copies(target, *inst.parent())) {
      return false;
    }
    if (!is_fallthrough(*inst.parent(), target)) {
      writer_.inst("j", block_label(function_, target));
    }
    return true;
  }

  bool lower_cond_br(const Instruction &inst) {
    if (inst.num_operands() != 3 ||
        inst.operand(1)->value_kind() != ValueKind::BasicBlock ||
        inst.operand(2)->value_kind() != ValueKind::BasicBlock) {
      return fail("malformed cond_br instruction");
    }
    const BasicBlock &true_target =
        *static_cast<const BasicBlock *>(inst.operand(1));
    const BasicBlock &false_target =
        *static_cast<const BasicBlock *>(inst.operand(2));
    const bool edge_copies_needed = block_starts_with_phi(true_target) ||
                                    block_starts_with_phi(false_target);
    const std::string true_copy_label = edge_copy_label(*inst.parent(), true_target);
    if (try_lower_direct_compare_branch(inst, true_target, false_target,
                                        true_copy_label)) {
      return true;
    }
    RvReg condition = RvReg::T0;
    if (!select_i32_register(inst.operand(0), RvReg::T0, condition)) {
      return false;
    }
    if (options_.opt_mode && !edge_copies_needed &&
        is_fallthrough(*inst.parent(), true_target)) {
      writer_.inst("beq", reg_name(condition), reg_name(RvReg::Zero),
                   block_label(function_, false_target));
      return true;
    }
    if (options_.opt_mode && !edge_copies_needed &&
        is_fallthrough(*inst.parent(), false_target)) {
      writer_.inst("bne", reg_name(condition), reg_name(RvReg::Zero),
                   block_label(function_, true_target));
      return true;
    }
    writer_.inst("bne", reg_name(condition), reg_name(RvReg::Zero),
                 true_copy_label);
    if (!emit_phi_copies(false_target, *inst.parent())) {
      return false;
    }
    writer_.inst("j", block_label(function_, false_target));
    writer_.label(true_copy_label);
    if (!emit_phi_copies(true_target, *inst.parent())) {
      return false;
    }
    if (!is_fallthrough(*inst.parent(), true_target)) {
      writer_.inst("j", block_label(function_, true_target));
    }
    return true;
  }

  bool try_lower_direct_compare_branch(const Instruction &branch,
                                       const BasicBlock &true_target,
                                       const BasicBlock &false_target,
                                       const std::string &true_copy_label) {
    if (branch.operand(0)->value_kind() != ValueKind::Register) {
      return false;
    }
    const Instruction *cmp = static_cast<const Instruction *>(branch.operand(0));
    if (!is_icmp_opcode(cmp->opcode()) || !is_direct_branch_condition(*cmp)) {
      return false;
    }
    RvReg lhs = RvReg::T0;
    RvReg rhs = RvReg::T1;
    if (!select_i32_register(cmp->operand(0), RvReg::T0, lhs) ||
        !select_i32_register(cmp->operand(1), RvReg::T1, rhs)) {
      return false;
    }

    if (options_.opt_mode && !block_starts_with_phi(true_target) &&
        !block_starts_with_phi(false_target)) {
      if (is_fallthrough(*branch.parent(), true_target)) {
        switch (cmp->opcode()) {
        case Opcode::ICmpEq:
          writer_.inst("bne", reg_name(lhs), reg_name(rhs),
                       block_label(function_, false_target));
          return true;
        case Opcode::ICmpNe:
          writer_.inst("beq", reg_name(lhs), reg_name(rhs),
                       block_label(function_, false_target));
          return true;
        case Opcode::ICmpSlt:
          writer_.inst("bge", reg_name(lhs), reg_name(rhs),
                       block_label(function_, false_target));
          return true;
        case Opcode::ICmpSgt:
          writer_.inst("bge", reg_name(rhs), reg_name(lhs),
                       block_label(function_, false_target));
          return true;
        case Opcode::ICmpSle:
          writer_.inst("blt", reg_name(rhs), reg_name(lhs),
                       block_label(function_, false_target));
          return true;
        case Opcode::ICmpSge:
          writer_.inst("blt", reg_name(lhs), reg_name(rhs),
                       block_label(function_, false_target));
          return true;
        default:
          break;
        }
      } else if (is_fallthrough(*branch.parent(), false_target)) {
        switch (cmp->opcode()) {
        case Opcode::ICmpEq:
          writer_.inst("beq", reg_name(lhs), reg_name(rhs),
                       block_label(function_, true_target));
          return true;
        case Opcode::ICmpNe:
          writer_.inst("bne", reg_name(lhs), reg_name(rhs),
                       block_label(function_, true_target));
          return true;
        case Opcode::ICmpSlt:
          writer_.inst("blt", reg_name(lhs), reg_name(rhs),
                       block_label(function_, true_target));
          return true;
        case Opcode::ICmpSgt:
          writer_.inst("blt", reg_name(rhs), reg_name(lhs),
                       block_label(function_, true_target));
          return true;
        case Opcode::ICmpSle:
          writer_.inst("bge", reg_name(rhs), reg_name(lhs),
                       block_label(function_, true_target));
          return true;
        case Opcode::ICmpSge:
          writer_.inst("bge", reg_name(lhs), reg_name(rhs),
                       block_label(function_, true_target));
          return true;
        default:
          break;
        }
      }
    }

    switch (cmp->opcode()) {
    case Opcode::ICmpEq:
      writer_.inst("beq", reg_name(lhs), reg_name(rhs),
                   true_copy_label);
      break;
    case Opcode::ICmpNe:
      writer_.inst("bne", reg_name(lhs), reg_name(rhs),
                   true_copy_label);
      break;
    case Opcode::ICmpSlt:
      writer_.inst("blt", reg_name(lhs), reg_name(rhs),
                   true_copy_label);
      break;
    case Opcode::ICmpSgt:
      writer_.inst("blt", reg_name(rhs), reg_name(lhs),
                   true_copy_label);
      break;
    case Opcode::ICmpSle:
      writer_.inst("bge", reg_name(rhs), reg_name(lhs),
                   true_copy_label);
      break;
    case Opcode::ICmpSge:
      writer_.inst("bge", reg_name(lhs), reg_name(rhs),
                   true_copy_label);
      break;
    default:
      return false;
    }

    if (!emit_phi_copies(false_target, *branch.parent())) {
      return false;
    }
    writer_.inst("j", block_label(function_, false_target));
    writer_.label(true_copy_label);
    if (!emit_phi_copies(true_target, *branch.parent())) {
      return false;
    }
    if (!is_fallthrough(*branch.parent(), true_target)) {
      writer_.inst("j", block_label(function_, true_target));
    }
    return true;
  }

  bool emit_phi_copies(const BasicBlock &target, const BasicBlock &predecessor) {
    struct PhiCopy {
      const Instruction *phi;
      Value *incoming;
      RvReg destination;
      bool has_forced_source = false;
      RvReg forced_source = RvReg::Zero;
    };

    std::vector<PhiCopy> register_copies;
    bool all_register_destinations = true;
    for (const std::unique_ptr<Instruction> &inst : target.insts()) {
      if (inst->opcode() != Opcode::Phi) {
        break;
      }
      Value *incoming =
          incoming_for_pred(static_cast<const PhiInst &>(*inst), predecessor);
      if (!incoming) {
        return fail("phi missing incoming value for predecessor");
      }
      auto destination = value_regs_.find(inst.get());
      if (destination == value_regs_.end()) {
        all_register_destinations = false;
        break;
      }
      register_copies.push_back(
          PhiCopy{inst.get(), incoming, destination->second});
    }

    // Most loop phis have distinct result and back-edge registers.  Lower
    // those edges directly and keep the existing two-phase stack copy as the
    // correctness fallback for register cycles or spilled phi results.
    if (all_register_destinations && !register_copies.empty()) {
      register_copies.erase(
          std::remove_if(register_copies.begin(), register_copies.end(),
                         [&](const PhiCopy &copy) {
                           auto source = value_regs_.find(copy.incoming);
                           return source != value_regs_.end() &&
                                  source->second == copy.destination;
                         }),
          register_copies.end());

      auto source_register = [&](const PhiCopy &copy, RvReg &source) {
        if (copy.has_forced_source) {
          source = copy.forced_source;
          return true;
        }
        auto allocated = value_regs_.find(copy.incoming);
        if (allocated == value_regs_.end()) return false;
        source = allocated->second;
        return true;
      };
      while (!register_copies.empty()) {
        auto ready = register_copies.end();
        for (auto candidate = register_copies.begin();
             candidate != register_copies.end(); ++candidate) {
          bool destination_is_source = false;
          for (const PhiCopy &other : register_copies) {
            RvReg source = RvReg::Zero;
            if (&other != &*candidate && source_register(other, source) &&
                source == candidate->destination) {
              destination_is_source = true;
              break;
            }
          }
          if (!destination_is_source) {
            ready = candidate;
            break;
          }
        }

        if (ready == register_copies.end()) {
          const RvReg preserved = register_copies.front().destination;
          emit_reg_copy(RvReg::T2, preserved);
          for (PhiCopy &copy : register_copies) {
            RvReg source = RvReg::Zero;
            if (source_register(copy, source) && source == preserved) {
              copy.has_forced_source = true;
              copy.forced_source = RvReg::T2;
            }
          }
          continue;
        }

        if (ready->has_forced_source) {
          emit_reg_copy(ready->destination, ready->forced_source);
        } else {
          auto source = value_regs_.find(ready->incoming);
          if (source != value_regs_.end()) {
            emit_reg_copy(ready->destination, source->second);
          } else {
            if (!load_i32(ready->incoming, RvReg::T0)) return false;
            emit_reg_copy(ready->destination, RvReg::T0);
          }
        }
        register_copies.erase(ready);
      }
      return true;
    }

    unsigned index = 0;
    for (const std::unique_ptr<Instruction> &inst : target.insts()) {
      if (inst->opcode() != Opcode::Phi) {
        break;
      }
      const PhiInst &phi = static_cast<const PhiInst &>(*inst);
      Value *incoming = incoming_for_pred(phi, predecessor);
      if (!incoming) {
        return fail("phi missing incoming value for predecessor");
      }
      if (!load_i32(incoming, RvReg::T0)) {
        return false;
      }
      emit_store_from_base(RvReg::T0, RvReg::Sp,
                           frame_.phi_temp_offset(index), RvReg::T2);
      ++index;
    }

    index = 0;
    for (const std::unique_ptr<Instruction> &inst : target.insts()) {
      if (inst->opcode() != Opcode::Phi) {
        break;
      }
      emit_load_from_base(RvReg::T0, RvReg::Sp,
                          frame_.phi_temp_offset(index), RvReg::T0);
      auto destination = value_regs_.find(inst.get());
      if (destination != value_regs_.end()) {
        emit_reg_copy(destination->second, RvReg::T0);
      } else {
        emit_store_from_base(RvReg::T0, RvReg::Sp,
                             frame_.slot_offset(inst.get()), RvReg::T2);
      }
      ++index;
    }
    return true;
  }

  Value *incoming_for_pred(const PhiInst &phi, const BasicBlock &predecessor) const {
    for (unsigned i = 0; i < phi.num_operands(); ++i) {
      if (phi.incoming_blocks()[i] == &predecessor) {
        return phi.operand(i);
      }
    }
    return nullptr;
  }

  std::string edge_copy_label(const BasicBlock &from, const BasicBlock &to) const {
    return ".L" + function_.short_name() + "_edge_" + from.name() + "_to_" +
           to.name();
  }

  bool lower_call(const Instruction &inst) {
    const CallInst &call = static_cast<const CallInst &>(inst);
    // Stack arguments are evaluated first: assigning a0-a7 may overwrite a
    // register that still supplies a later stack argument.
    for (unsigned i = static_cast<unsigned>(arg_regs_.size());
         i < inst.num_operands(); ++i) {
      if (!load_i32(inst.operand(i), RvReg::T0)) return false;
      emit_store_from_base(RvReg::T0, RvReg::Sp,
                           static_cast<int>((i - 8) * 4), RvReg::T2);
    }

    struct ArgCopy {
      Value *value;
      RvReg destination;
      bool forced = false;
      RvReg forced_source = RvReg::Zero;
    };
    std::vector<ArgCopy> copies;
    const unsigned register_args = static_cast<unsigned>(
        std::min<std::size_t>(inst.num_operands(), arg_regs_.size()));
    for (unsigned i = 0; i < register_args; ++i) {
      auto source = value_regs_.find(inst.operand(i));
      if (source != value_regs_.end() && source->second == arg_regs_[i]) continue;
      copies.push_back({inst.operand(i), arg_regs_[i]});
    }
    auto source_register = [&](const ArgCopy &copy, RvReg &source) {
      if (copy.forced) {
        source = copy.forced_source;
        return true;
      }
      auto allocated = value_regs_.find(copy.value);
      if (allocated == value_regs_.end()) return false;
      source = allocated->second;
      return true;
    };
    while (!copies.empty()) {
      auto ready = copies.end();
      for (auto candidate = copies.begin(); candidate != copies.end(); ++candidate) {
        bool destination_needed = false;
        for (const ArgCopy &other : copies) {
          RvReg source = RvReg::Zero;
          if (&other != &*candidate && source_register(other, source) &&
              source == candidate->destination) {
            destination_needed = true;
            break;
          }
        }
        if (!destination_needed) {
          ready = candidate;
          break;
        }
      }
      if (ready == copies.end()) {
        const RvReg preserved = copies.front().destination;
        emit_reg_copy(RvReg::T2, preserved);
        for (ArgCopy &copy : copies) {
          RvReg source = RvReg::Zero;
          if (source_register(copy, source) && source == preserved) {
            copy.forced = true;
            copy.forced_source = RvReg::T2;
          }
        }
        continue;
      }
      if (ready->forced) {
        emit_reg_copy(ready->destination, ready->forced_source);
      } else {
        auto source = value_regs_.find(ready->value);
        if (source != value_regs_.end()) {
          emit_reg_copy(ready->destination, source->second);
        } else if (!load_i32(ready->value, ready->destination)) {
          return false;
        }
      }
      copies.erase(ready);
    }
    writer_.inst("call", call.callee_name());
    if (inst.has_result()) {
      return spill_result(inst, RvReg::A0);
    }
    return true;
  }

  bool lower_ret(const Instruction &inst) {
    if (inst.num_operands() > 1) {
      return fail("malformed ret instruction");
    }
    if (inst.num_operands() == 1 && !load_i32(inst.operand(0), RvReg::A0)) {
      return false;
    }
    writer_.inst("j", exit_label());
    return true;
  }

  void emit_prologue() {
    writer_.global(function_label(function_));
    writer_.label(function_label(function_));
    if (frame_.frame_size() > 0) {
      emit_stack_adjust(-frame_.frame_size());
    }
    if (frame_.saves_ra()) {
      emit_store_from_base(RvReg::Ra, RvReg::Sp, frame_.ra_offset(), RvReg::T0);
    }
    for (RvReg reg : used_callee_saved_) {
      emit_store_from_base(reg, RvReg::Sp, frame_.callee_saved_offset(reg),
                           RvReg::T0);
    }
    for (unsigned i = 0; i < function_.params().size(); ++i) {
      Value *param = function_.param(i);
      auto allocated = value_regs_.find(param);
      if (allocated != value_regs_.end()) {
        if (i < arg_regs_.size()) {
          emit_reg_copy(allocated->second, arg_regs_[i]);
        } else {
          emit_load_from_base(allocated->second, RvReg::Sp,
                              frame_.stack_param_offset(i), RvReg::T0);
        }
      } else if (i < arg_regs_.size()) {
        emit_store_from_base(arg_regs_[i], RvReg::Sp,
                             frame_.register_param_offset(i), RvReg::T0);
      }
    }
    for (const auto &cached : cached_constants_) {
      materialize_i32(cached.first->value(), cached.second);
    }
  }

  void emit_epilogue() {
    if (emitted_exit_) {
      return;
    }
    for (auto it = used_callee_saved_.rbegin();
         it != used_callee_saved_.rend(); ++it) {
      emit_load_from_base(*it, RvReg::Sp, frame_.callee_saved_offset(*it),
                          RvReg::T0);
    }
    if (frame_.saves_ra()) {
      emit_load_from_base(RvReg::Ra, RvReg::Sp, frame_.ra_offset(), RvReg::T0);
    }
    if (frame_.frame_size() > 0) {
      emit_stack_adjust(frame_.frame_size());
    }
    if (function_.short_name() == "main" && options_.emit_exit_syscall) {
      materialize_i32(93, RvReg::A7);
      writer_.inst("ecall");
    } else {
      writer_.inst("jalr", reg_name(RvReg::Zero), offset_addr(0, RvReg::Ra));
    }
    emitted_exit_ = true;
  }

  std::string exit_label() const {
    return ".L" + function_.short_name() + "_exit";
  }

  bool load_i32(Value *value, RvReg dst) {
    auto allocated = value_regs_.find(value);
    if (allocated != value_regs_.end()) {
      emit_reg_copy(dst, allocated->second);
      return true;
    }
    if (value->value_kind() == ValueKind::Constant) {
      materialize_i32(static_cast<const ConstantInt *>(value)->value(), dst);
      return true;
    }
    if (value->value_kind() == ValueKind::Param) {
      const unsigned id = value->id();
      if (frame_.has_register_param_slot(id)) {
        emit_load_from_base(dst, RvReg::Sp, frame_.register_param_offset(id), dst);
        return true;
      }
      emit_load_from_base(dst, RvReg::Sp, frame_.stack_param_offset(id), dst);
      return true;
    }
    if (frame_.has_slot(value)) {
      emit_load_from_base(dst, RvReg::Sp, frame_.slot_offset(value), dst);
      return true;
    }
    return fail("codegen cannot materialize value " + value->name());
  }

  bool select_i32_register(Value *value, RvReg scratch, RvReg &selected) {
    if (options_.opt_mode && value->value_kind() == ValueKind::Constant &&
        static_cast<const ConstantInt *>(value)->value() == 0) {
      selected = RvReg::Zero;
      return true;
    }
    auto allocated = value_regs_.find(value);
    if (allocated != value_regs_.end()) {
      selected = allocated->second;
      return true;
    }
    if (!load_i32(value, scratch)) {
      return false;
    }
    selected = scratch;
    return true;
  }

  RvReg result_register(const Instruction &inst, RvReg fallback) const {
    auto allocated = value_regs_.find(&inst);
    return allocated == value_regs_.end() ? fallback : allocated->second;
  }

  bool store_i32(RvReg src, Value *destination_ptr) {
    if (!load_address(destination_ptr, RvReg::T0)) {
      return false;
    }
    writer_.inst("sw", reg_name(src), offset_addr(0, RvReg::T0));
    return true;
  }

  bool load_address(Value *ptr, RvReg dst) {
    if (ptr->value_kind() == ValueKind::GlobalAddr) {
      const std::string label =
          global_label(*static_cast<const GlobalAddr *>(ptr));
      writer_.inst("lui", reg_name(dst), "%hi(" + label + ")");
      writer_.inst("addi", reg_name(dst), reg_name(dst), "%lo(" + label + ")");
      return true;
    }
    if (frame_.has_slot(ptr)) {
      emit_add_imm(dst, RvReg::Sp, frame_.slot_offset(ptr), dst);
      return true;
    }
    return fail("codegen cannot materialize address " + ptr->name());
  }

  bool spill_result(const Instruction &inst, RvReg src) {
    auto reg = value_regs_.find(&inst);
    if (reg != value_regs_.end()) {
      emit_reg_copy(reg->second, src);
      return true;
    }
    if (!frame_.has_slot(&inst)) {
      return fail("codegen has no result slot for " + inst.name());
    }
    emit_store_from_base(src, RvReg::Sp, frame_.slot_offset(&inst), RvReg::T0);
    return true;
  }

  void materialize_i32(int value, RvReg dst) {
    if (options_.allow_pseudo) {
      writer_.inst("li", reg_name(dst), std::to_string(value));
      return;
    }
    if (fits_i12(value)) {
      emit_addi(dst, RvReg::Zero, value);
      return;
    }
    const int64_t rounded = static_cast<int64_t>(value) + 0x800;
    const int64_t raw_hi = rounded >> 12;
    int64_t hi = raw_hi;
    if (hi < 0) {
      hi += 1 << 20;
    }
    const int64_t lo = static_cast<int64_t>(value) - (raw_hi << 12);
    writer_.inst("lui", reg_name(dst), std::to_string(hi));
    if (lo != 0) {
      writer_.inst("addi", reg_name(dst), reg_name(dst), std::to_string(lo));
    }
  }

  bool try_lower_immediate_binary(const Instruction &inst) {
    if (inst.opcode() != Opcode::Add && inst.opcode() != Opcode::Sub) {
      return false;
    }
    Value *lhs = inst.operand(0);
    Value *rhs = inst.operand(1);
    if (rhs->value_kind() == ValueKind::Constant) {
      int imm = static_cast<const ConstantInt *>(rhs)->value();
      if (inst.opcode() == Opcode::Sub) {
        if (imm == std::numeric_limits<int>::min()) {
          return false;
        }
        imm = -imm;
      }
      if (fits_i12(imm)) {
        RvReg source = RvReg::T0;
        if (!select_i32_register(lhs, RvReg::T0, source)) {
          return false;
        }
        const RvReg destination = result_register(inst, RvReg::T2);
        emit_addi(destination, source, imm);
        return spill_result(inst, destination);
      }
      return false;
    }
    if (inst.opcode() == Opcode::Add && lhs->value_kind() == ValueKind::Constant) {
      const int imm = static_cast<const ConstantInt *>(lhs)->value();
      if (fits_i12(imm)) {
        RvReg source = RvReg::T0;
        if (!select_i32_register(rhs, RvReg::T0, source)) {
          return false;
        }
        const RvReg destination = result_register(inst, RvReg::T2);
        emit_addi(destination, source, imm);
        return spill_result(inst, destination);
      }
    }
    return false;
  }

  void emit_addi(RvReg dst, RvReg src, int imm) {
    if (imm == 0 && dst == src) {
      return;
    }
    writer_.inst("addi", reg_name(dst), reg_name(src), std::to_string(imm));
  }

  void emit_add_imm(RvReg dst, RvReg src, int imm, RvReg scratch) {
    if (fits_i12(imm)) {
      emit_addi(dst, src, imm);
      return;
    }
    materialize_i32(imm, scratch);
    writer_.inst("add", reg_name(dst), reg_name(src), reg_name(scratch));
  }

  void emit_stack_adjust(int bytes) {
    emit_add_imm(RvReg::Sp, RvReg::Sp, bytes, RvReg::T0);
  }

  void emit_load_from_base(RvReg dst, RvReg base, int offset, RvReg scratch) {
    if (fits_i12(offset)) {
      writer_.inst("lw", reg_name(dst), offset_addr(offset, base));
      return;
    }
    emit_add_imm(scratch, base, offset, scratch);
    writer_.inst("lw", reg_name(dst), offset_addr(0, scratch));
  }

  void emit_store_from_base(RvReg src, RvReg base, int offset, RvReg scratch) {
    if (fits_i12(offset)) {
      writer_.inst("sw", reg_name(src), offset_addr(offset, base));
      return;
    }
    emit_add_imm(scratch, base, offset, scratch);
    writer_.inst("sw", reg_name(src), offset_addr(0, scratch));
  }

  void emit_reg_copy(RvReg dst, RvReg src) {
    if (dst == src) {
      return;
    }
    writer_.inst("add", reg_name(dst), reg_name(src), reg_name(RvReg::Zero));
  }

  bool is_direct_branch_condition(const Instruction &inst) const {
    return is_direct_branch_condition_value(inst);
  }

  static bool is_icmp_opcode(Opcode opcode) {
    return opcode == Opcode::ICmpEq || opcode == Opcode::ICmpNe ||
           opcode == Opcode::ICmpSlt || opcode == Opcode::ICmpSgt ||
           opcode == Opcode::ICmpSle || opcode == Opcode::ICmpSge;
  }

  bool is_fallthrough(const BasicBlock &from, const BasicBlock &to) const {
    for (std::size_t i = 0; i + 1 < layout_.size(); ++i) {
      if (layout_[i] == &from) return layout_[i + 1] == &to;
    }
    return false;
  }

  void plan_block_layout() {
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      layout_.push_back(block.get());
    }
    if (!options_.opt_mode || layout_.size() < 4) return;

    // Rotate the canonical IRGen while layout
    //   preheader, header, body, exit
    // into
    //   preheader, body, header, exit.
    // The preheader pays one initial jump, while every hot iteration loses its
    // unconditional back-edge jump.
    for (std::size_t i = 1; i + 2 < layout_.size(); ++i) {
      const BasicBlock *header = layout_[i];
      const BasicBlock *body = layout_[i + 1];
      const BasicBlock *exit = layout_[i + 2];
      const Instruction *header_term = header->terminator();
      const Instruction *body_term = body->terminator();
      if (!header_term || header_term->opcode() != Opcode::CondBr ||
          !body_term || body_term->opcode() != Opcode::Br) {
        continue;
      }
      if (header_term->operand(1) != body || header_term->operand(2) != exit ||
          body_term->operand(0) != header) {
        continue;
      }
      layout_[i] = body;
      layout_[i + 1] = header;
      layout_[i + 2] = exit;
      i += 2;
    }
  }

  static bool is_register_candidate(const Instruction &inst) {
    switch (inst.opcode()) {
    case Opcode::Load:
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::Sdiv:
    case Opcode::Srem:
    case Opcode::Neg:
    case Opcode::ICmpEq:
    case Opcode::ICmpNe:
    case Opcode::ICmpSlt:
    case Opcode::ICmpSgt:
    case Opcode::ICmpSle:
    case Opcode::ICmpSge:
    case Opcode::Shl:
    case Opcode::Shr:
      return true;
    default:
      return false;
    }
  }

  static bool block_starts_with_phi(const BasicBlock &block) {
    return !block.insts().empty() &&
           block.insts().front()->opcode() == Opcode::Phi;
  }

  using ValueSet = std::unordered_set<const Value *>;
  using InterferenceGraph =
      std::unordered_map<const Value *, ValueSet>;

  static std::vector<const BasicBlock *> successors(const BasicBlock &block) {
    std::vector<const BasicBlock *> result;
    const Instruction *term = block.terminator();
    if (!term) {
      return result;
    }
    if (term->opcode() == Opcode::Br) {
      result.push_back(static_cast<const BasicBlock *>(term->operand(0)));
    } else if (term->opcode() == Opcode::CondBr) {
      result.push_back(static_cast<const BasicBlock *>(term->operand(1)));
      result.push_back(static_cast<const BasicBlock *>(term->operand(2)));
    }
    return result;
  }

  static void add_interference(InterferenceGraph &graph, const Value *a,
                               const Value *b) {
    if (a == b) {
      return;
    }
    graph[a].insert(b);
    graph[b].insert(a);
  }

  static void add_clique(InterferenceGraph &graph, const ValueSet &values) {
    for (auto first = values.begin(); first != values.end(); ++first) {
      auto second = first;
      ++second;
      for (; second != values.end(); ++second) {
        add_interference(graph, *first, *second);
      }
    }
  }

  static bool sets_equal(const ValueSet &lhs, const ValueSet &rhs) {
    if (lhs.size() != rhs.size()) {
      return false;
    }
    for (const Value *value : lhs) {
      if (!rhs.count(value)) {
        return false;
      }
    }
    return true;
  }

  Value *phi_incoming_for_edge(const PhiInst &phi,
                               const BasicBlock &predecessor) const {
    for (unsigned i = 0; i < phi.num_operands(); ++i) {
      if (phi.incoming_blocks()[i] == &predecessor) {
        return phi.operand(i);
      }
    }
    return nullptr;
  }

  void plan_global_registers() {
    ValueSet candidates;
    std::vector<const Value *> candidate_order;
    auto add_candidate = [&](const Value *value) {
      if (candidates.insert(value).second) {
        candidate_order.push_back(value);
      }
    };
    for (const std::unique_ptr<Value> &param : function_.params()) {
      if (!param->uses().empty()) {
        add_candidate(param.get());
      }
    }
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (inst->has_result() && inst->opcode() != Opcode::Alloca &&
            inst->type() == Type::I32 && !inst->uses().empty()) {
          if (is_direct_branch_condition(*inst)) {
            continue;
          }
          add_candidate(inst.get());
        }
      }
    }
    if (candidates.empty()) {
      return;
    }

    std::unordered_map<const BasicBlock *, ValueSet> block_use;
    std::unordered_map<const BasicBlock *, ValueSet> block_def;
    std::unordered_map<const BasicBlock *, ValueSet> live_in;
    std::unordered_map<const BasicBlock *, ValueSet> live_out;
    for (const std::unique_ptr<BasicBlock> &block_owner : function_.blocks()) {
      const BasicBlock *block = block_owner.get();
      ValueSet &uses = block_use[block];
      ValueSet &defs = block_def[block];
      for (const std::unique_ptr<Instruction> &inst_owner : block->insts()) {
        const Instruction *inst = inst_owner.get();
        if (inst->opcode() != Opcode::Phi) {
          for (Value *operand : inst->operands()) {
            if (candidates.count(operand) && !defs.count(operand)) {
              uses.insert(operand);
            }
          }
        }
        if (candidates.count(inst)) {
          defs.insert(inst);
        }
      }
      live_in[block];
      live_out[block];
    }

    bool changed = true;
    while (changed) {
      changed = false;
      for (auto block_it = function_.blocks().rbegin();
           block_it != function_.blocks().rend(); ++block_it) {
        const BasicBlock *block = block_it->get();
        ValueSet new_out;
        for (const BasicBlock *successor : successors(*block)) {
          ValueSet edge_live = live_in[successor];
          for (const std::unique_ptr<Instruction> &inst : successor->insts()) {
            if (inst->opcode() != Opcode::Phi) {
              break;
            }
            edge_live.erase(inst.get());
            Value *incoming = phi_incoming_for_edge(
                static_cast<const PhiInst &>(*inst), *block);
            if (incoming && candidates.count(incoming)) {
              edge_live.insert(incoming);
            }
          }
          new_out.insert(edge_live.begin(), edge_live.end());
        }
        ValueSet new_in = block_use[block];
        for (const Value *value : new_out) {
          if (!block_def[block].count(value)) {
            new_in.insert(value);
          }
        }
        if (!sets_equal(new_out, live_out[block]) ||
            !sets_equal(new_in, live_in[block])) {
          live_out[block] = std::move(new_out);
          live_in[block] = std::move(new_in);
          changed = true;
        }
      }
    }

    InterferenceGraph graph;
    ValueSet crosses_call;
    for (const Value *candidate : candidates) {
      graph[candidate];
    }
    for (const std::unique_ptr<BasicBlock> &block_owner : function_.blocks()) {
      const BasicBlock *block = block_owner.get();
      ValueSet live = live_out[block];
      add_clique(graph, live);
      for (auto inst_it = block->insts().rbegin();
           inst_it != block->insts().rend(); ++inst_it) {
        const Instruction *inst = inst_it->get();
        if (candidates.count(inst)) {
          live.erase(inst);
          for (const Value *other : live) {
            add_interference(graph, inst, other);
          }
        }
        if (inst->opcode() == Opcode::Call) {
          crosses_call.insert(live.begin(), live.end());
        }
        if (inst->opcode() != Opcode::Phi) {
          for (Value *operand : inst->operands()) {
            if (candidates.count(operand)) {
              live.insert(operand);
            }
          }
        }
        add_clique(graph, live);
      }
    }

    std::vector<const Value *> order = candidate_order;
    std::unordered_map<const BasicBlock *, int> block_weight;
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      block_weight[block.get()] = block_is_cyclic(*block) ? 16 : 1;
    }
    std::unordered_map<const Value *, int> weighted_uses;
    for (const Value *candidate : candidates) {
      int score = 0;
      for (const User *user : candidate->uses()) {
        const Instruction *inst = dynamic_cast<const Instruction *>(user);
        score += inst && inst->parent() ? block_weight[inst->parent()] : 1;
      }
      const Instruction *definition =
          dynamic_cast<const Instruction *>(candidate);
      if (definition && definition->parent()) {
        score += block_weight[definition->parent()];
      }
      weighted_uses[candidate] = score;
    }
    std::unordered_map<const Value *, std::size_t> stable_index;
    for (std::size_t i = 0; i < candidate_order.size(); ++i) {
      stable_index.emplace(candidate_order[i], i);
    }
    auto priority = [&](const Value *value) {
      int score = weighted_uses[value] * 10;
      if (value->value_kind() == ValueKind::Param) {
        score += 500;
      }
      const Instruction *inst = dynamic_cast<const Instruction *>(value);
      if (inst && inst->opcode() == Opcode::Phi) {
        score += 100;
      }
      return score;
    };
    std::sort(order.begin(), order.end(), [&](const Value *lhs,
                                               const Value *rhs) {
      const int lhs_priority = priority(lhs);
      const int rhs_priority = priority(rhs);
      if (lhs_priority != rhs_priority) {
        return lhs_priority > rhs_priority;
      }
      if (graph[lhs].size() != graph[rhs].size()) {
        return graph[lhs].size() > graph[rhs].size();
      }
      if (lhs->id() != rhs->id()) {
        return lhs->id() < rhs->id();
      }
      return stable_index[lhs] < stable_index[rhs];
    });

    const std::vector<RvReg> caller_saved = {
        RvReg::T3, RvReg::T4, RvReg::T5, RvReg::T6};
    const std::vector<RvReg> leaf_arg_registers = {
        RvReg::A0, RvReg::A1, RvReg::A2, RvReg::A3,
        RvReg::A4, RvReg::A5, RvReg::A6, RvReg::A7};
    const std::vector<RvReg> callee_saved = {
        RvReg::S1, RvReg::S2, RvReg::S3, RvReg::S4, RvReg::S5, RvReg::S6,
        RvReg::S7, RvReg::S8, RvReg::S9, RvReg::S10, RvReg::S11};
    for (const Value *value : order) {
      std::unordered_set<RvReg> unavailable;
      for (const Value *neighbor : graph[value]) {
        auto allocated = value_regs_.find(neighbor);
        if (allocated != value_regs_.end()) {
          unavailable.insert(allocated->second);
        }
      }
      std::vector<RvReg> registers;
      if (!crosses_call.count(value)) {
        registers.insert(registers.end(), caller_saved.begin(),
                         caller_saved.end());
        if (value->value_kind() == ValueKind::Param && value->id() < 8) {
          registers.insert(registers.begin(),
                           leaf_arg_registers[value->id()]);
        }
        registers.insert(registers.end(), leaf_arg_registers.begin(),
                         leaf_arg_registers.end());
      }
      registers.insert(registers.end(), callee_saved.begin(),
                       callee_saved.end());
      for (RvReg reg : registers) {
        if (!unavailable.count(reg)) {
          value_regs_.emplace(value, reg);
          break;
        }
      }
    }
  }

  void collect_used_callee_saved() {
    const std::vector<RvReg> registers = {
        RvReg::S1, RvReg::S2, RvReg::S3, RvReg::S4, RvReg::S5, RvReg::S6,
        RvReg::S7, RvReg::S8, RvReg::S9, RvReg::S10, RvReg::S11};
    for (RvReg reg : registers) {
      for (const auto &allocation : value_regs_) {
        if (allocation.second == reg) {
          used_callee_saved_.push_back(reg);
          break;
        }
      }
    }
  }

  bool block_is_cyclic(const BasicBlock &start) const {
    std::vector<const BasicBlock *> work = successors(start);
    std::unordered_set<const BasicBlock *> visited;
    while (!work.empty()) {
      const BasicBlock *block = work.back();
      work.pop_back();
      if (block == &start) return true;
      if (!visited.insert(block).second) continue;
      const std::vector<const BasicBlock *> next = successors(*block);
      work.insert(work.end(), next.begin(), next.end());
    }
    return false;
  }

  void plan_loop_constants() {
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (inst->opcode() == Opcode::Call) return;
      }
    }

    std::unordered_map<const ConstantInt *, int> hot_constants;
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      if (!block_is_cyclic(*block)) continue;
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (inst->opcode() == Opcode::Phi) continue;
        for (unsigned i = 0; i < inst->num_operands(); ++i) {
          Value *operand = inst->operand(i);
          if (operand->value_kind() != ValueKind::Constant) continue;
          const ConstantInt *constant = static_cast<const ConstantInt *>(operand);
          if (constant->value() == 0) continue;
          const bool immediate_add =
              (inst->opcode() == Opcode::Add || inst->opcode() == Opcode::Sub) &&
              (i == 1 || inst->opcode() == Opcode::Add);
          if (!immediate_add) ++hot_constants[constant];
        }
      }
    }

    std::vector<std::pair<const ConstantInt *, int>> ordered(
        hot_constants.begin(), hot_constants.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto &lhs, const auto &rhs) {
      if (lhs.second != rhs.second) return lhs.second > rhs.second;
      return lhs.first->value() < rhs.first->value();
    });
    const std::vector<RvReg> registers = {
        RvReg::T3, RvReg::T4, RvReg::T5, RvReg::T6};
    std::unordered_set<RvReg> used;
    for (const auto &allocation : value_regs_) used.insert(allocation.second);
    for (const auto &candidate : ordered) {
      for (RvReg reg : registers) {
        if (used.count(reg)) continue;
        value_regs_.emplace(candidate.first, reg);
        cached_constants_.push_back({candidate.first, reg});
        used.insert(reg);
        break;
      }
    }
  }

  void plan_local_registers() {
    const std::vector<RvReg> temp_regs = {RvReg::T3, RvReg::T4, RvReg::T5,
                                          RvReg::T6};
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      std::unordered_map<const Instruction *, int> index_by_inst;
      std::vector<int> call_indices;
      int index = 0;
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        index_by_inst.emplace(inst.get(), index);
        if (inst->opcode() == Opcode::Call) {
          call_indices.push_back(index);
        }
        ++index;
      }

      struct Interval {
        const Instruction *inst;
        int start;
        int end;
      };
      std::vector<Interval> intervals;
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (value_regs_.count(inst.get()) || !is_register_candidate(*inst) ||
            is_direct_branch_condition(*inst)) {
          continue;
        }
        const int start = index_by_inst[inst.get()];
        int end = start;
        bool eligible = !inst->uses().empty();
        for (const User *user : inst->uses()) {
          const Instruction *user_inst = dynamic_cast<const Instruction *>(user);
          if (!user_inst || user_inst->parent() != block.get()) {
            eligible = false;
            break;
          }
          auto found = index_by_inst.find(user_inst);
          if (found == index_by_inst.end() || found->second <= start) {
            eligible = false;
            break;
          }
          end = std::max(end, found->second);
        }
        for (int call_index : call_indices) {
          if (call_index > start && call_index <= end) {
            eligible = false;
            break;
          }
        }
        if (eligible) {
          intervals.push_back(Interval{inst.get(), start, end});
        }
      }

      std::vector<Interval> active;
      std::unordered_map<RvReg, bool> used;
      for (const Interval &interval : intervals) {
        active.erase(std::remove_if(active.begin(), active.end(), [&](const Interval &item) {
                       if (item.end < interval.start) {
                         used[value_regs_[item.inst]] = false;
                         return true;
                       }
                       return false;
                     }),
                     active.end());
        for (RvReg reg : temp_regs) {
          if (!used[reg]) {
            value_regs_.emplace(interval.inst, reg);
            used[reg] = true;
            active.push_back(interval);
            break;
          }
        }
      }
    }
  }

  bool fail(const std::string &message) {
    diagnostics_.error(DiagnosticStage::Codegen, SourceLoc{0, 0}, message);
    return false;
  }

  const Function &function_;
  const CodegenOptions &options_;
  DiagnosticEngine &diagnostics_;
  AsmWriter &writer_;
  FunctionFrame frame_;
  bool emitted_exit_ = false;
  std::unordered_map<const Value *, RvReg> value_regs_;
  std::vector<const BasicBlock *> layout_;
  std::vector<RvReg> used_callee_saved_;
  std::vector<std::pair<const ConstantInt *, RvReg>> cached_constants_;
  const std::vector<RvReg> arg_regs_ = {
      RvReg::A0, RvReg::A1, RvReg::A2, RvReg::A3,
      RvReg::A4, RvReg::A5, RvReg::A6, RvReg::A7,
  };
};

void emit_globals(const Module &module, AsmWriter &writer) {
  bool emitted_rodata = false;
  for (const std::unique_ptr<GlobalVar> &global : module.globals()) {
    if (!global->is_const) {
      continue;
    }
    if (!emitted_rodata) {
      writer.section(".rodata");
      emitted_rodata = true;
    }
    writer.global(global_label(*global->addr));
    writer.label(global_label(*global->addr));
    writer.inst(".word", std::to_string(global->init->value()));
  }

  bool emitted_data = false;
  for (const std::unique_ptr<GlobalVar> &global : module.globals()) {
    if (global->is_const) {
      continue;
    }
    if (!emitted_data) {
      writer.section(".data");
      emitted_data = true;
    }
    writer.global(global_label(*global->addr));
    writer.label(global_label(*global->addr));
    writer.inst(".word", std::to_string(global->init->value()));
  }
}

bool emit_functions(const Module &module, const CodegenOptions &options,
                    DiagnosticEngine &diagnostics, AsmWriter &writer) {
  writer.section(".text");
  for (const std::unique_ptr<Function> &function : module.functions()) {
    FunctionLowerer lowerer(*function, options, diagnostics, writer);
    if (!lowerer.lower()) {
      return false;
    }
  }
  return true;
}

} // namespace

bool emit_riscv(const Module &module, const CodegenOptions &options,
                DiagnosticEngine &diagnostics, std::ostream &out) {
  const Function *main = find_main(module);
  if (!main) {
    diagnostics.error(DiagnosticStage::Codegen, SourceLoc{0, 0},
                      "codegen requires an int main() function");
    return false;
  }

  std::ostringstream buffer;
  AsmWriter writer(buffer);
  emit_globals(module, writer);
  if (!emit_functions(module, options, diagnostics, writer)) {
    return false;
  }
  out << buffer.str();
  return true;
}

} // namespace toyc
