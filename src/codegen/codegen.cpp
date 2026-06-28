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

class FunctionFrame {
public:
  explicit FunctionFrame(const Function &function) {
    for (const std::unique_ptr<Value> &param : function.params()) {
      if (param->id() >= 8) {
        next_offset_ += 4;
      }
    }
    outgoing_arg_size_ = next_offset_;
    for (const std::unique_ptr<BasicBlock> &block : function.blocks()) {
      int block_phi_count = 0;
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (inst->opcode() == Opcode::Phi) {
          ++block_phi_count;
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
      max_phi_count_ = std::max(max_phi_count_, block_phi_count);
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
    for (unsigned i = 0; i < register_params; ++i) {
      param_offsets_.push_back(next_offset_);
      next_offset_ += 4;
    }
    for (const std::unique_ptr<BasicBlock> &block : function.blocks()) {
      for (const std::unique_ptr<Instruction> &inst : block->insts()) {
        if (inst->opcode() == Opcode::Alloca || is_result_slot_inst(*inst)) {
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
    return id < param_offsets_.size();
  }
  int register_param_offset(unsigned id) const { return param_offsets_[id]; }
  int phi_temp_offset(unsigned index) const { return phi_temp_offsets_[index]; }

  bool has_slot(const Value *value) const {
    return slots_.find(value) != slots_.end();
  }

  int slot_offset(const Value *value) const {
    auto found = slots_.find(value);
    return found == slots_.end() ? -1 : found->second;
  }

private:
  std::unordered_map<const Value *, int> slots_;
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
    if (options_.opt_mode) {
      plan_local_registers();
    }
  }

  bool lower() {
    if (!validate_function_shape()) {
      return false;
    }
    emit_prologue();
    for (const std::unique_ptr<BasicBlock> &block : function_.blocks()) {
      if (block.get() != function_.entry()) {
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
    writer_.inst("lw", reg_name(RvReg::T1), offset_addr(0, RvReg::T0));
    return spill_result(inst, RvReg::T1);
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
    if (inst.num_operands() != 2) {
      return fail("malformed binary instruction");
    }
    if (try_lower_immediate_binary(inst)) {
      return true;
    }
    if (!load_i32(inst.operand(0), RvReg::T0) ||
        !load_i32(inst.operand(1), RvReg::T1)) {
      return false;
    }
    if (inst.opcode() == Opcode::Add) {
      writer_.inst("add", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
    } else if (inst.opcode() == Opcode::Sub) {
      writer_.inst("sub", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
    } else if (inst.opcode() == Opcode::Mul) {
      writer_.inst("mul", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
    } else if (inst.opcode() == Opcode::Sdiv) {
      writer_.inst("div", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
    } else if (inst.opcode() == Opcode::Srem) {
      writer_.inst("rem", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
    } else if (inst.opcode() == Opcode::Shl) {
      writer_.inst("slli", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   std::to_string(static_cast<const ShlInst &>(inst).amount()));
    } else {
      writer_.inst("srai", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   std::to_string(static_cast<const ShrInst &>(inst).amount()));
    }
    return spill_result(inst, RvReg::T2);
  }

  bool lower_neg(const Instruction &inst) {
    if (inst.num_operands() != 1) {
      return fail("malformed neg instruction");
    }
    if (!load_i32(inst.operand(0), RvReg::T0)) {
      return false;
    }
    writer_.inst("sub", reg_name(RvReg::T2), reg_name(RvReg::Zero),
                 reg_name(RvReg::T0));
    return spill_result(inst, RvReg::T2);
  }

  bool lower_icmp(const Instruction &inst) {
    if (inst.num_operands() != 2) {
      return fail("malformed icmp instruction");
    }
    if (is_direct_branch_condition(inst)) {
      return true;
    }
    if (!load_i32(inst.operand(0), RvReg::T0) ||
        !load_i32(inst.operand(1), RvReg::T1)) {
      return false;
    }
    switch (inst.opcode()) {
    case Opcode::ICmpSlt:
      writer_.inst("slt", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
      break;
    case Opcode::ICmpSgt:
      writer_.inst("slt", reg_name(RvReg::T2), reg_name(RvReg::T1),
                   reg_name(RvReg::T0));
      break;
    case Opcode::ICmpSle:
      writer_.inst("slt", reg_name(RvReg::T2), reg_name(RvReg::T1),
                   reg_name(RvReg::T0));
      writer_.inst("xori", reg_name(RvReg::T2), reg_name(RvReg::T2), "1");
      break;
    case Opcode::ICmpSge:
      writer_.inst("slt", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
      writer_.inst("xori", reg_name(RvReg::T2), reg_name(RvReg::T2), "1");
      break;
    case Opcode::ICmpEq:
      writer_.inst("xor", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
      writer_.inst("sltiu", reg_name(RvReg::T2), reg_name(RvReg::T2), "1");
      break;
    case Opcode::ICmpNe:
      writer_.inst("xor", reg_name(RvReg::T2), reg_name(RvReg::T0),
                   reg_name(RvReg::T1));
      writer_.inst("sltu", reg_name(RvReg::T2), reg_name(RvReg::Zero),
                   reg_name(RvReg::T2));
      break;
    default:
      return fail("malformed icmp opcode");
    }
    return spill_result(inst, RvReg::T2);
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
    const std::string true_copy_label = edge_copy_label(*inst.parent(), true_target);
    if (try_lower_direct_compare_branch(inst, true_target, false_target,
                                        true_copy_label)) {
      return true;
    }
    if (!load_i32(inst.operand(0), RvReg::T0)) {
      return false;
    }
    writer_.inst("bne", reg_name(RvReg::T0), reg_name(RvReg::Zero),
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
    if (!load_i32(cmp->operand(0), RvReg::T0) ||
        !load_i32(cmp->operand(1), RvReg::T1)) {
      return false;
    }

    switch (cmp->opcode()) {
    case Opcode::ICmpEq:
      writer_.inst("beq", reg_name(RvReg::T0), reg_name(RvReg::T1),
                   true_copy_label);
      break;
    case Opcode::ICmpNe:
      writer_.inst("bne", reg_name(RvReg::T0), reg_name(RvReg::T1),
                   true_copy_label);
      break;
    case Opcode::ICmpSlt:
      writer_.inst("blt", reg_name(RvReg::T0), reg_name(RvReg::T1),
                   true_copy_label);
      break;
    case Opcode::ICmpSgt:
      writer_.inst("blt", reg_name(RvReg::T1), reg_name(RvReg::T0),
                   true_copy_label);
      break;
    case Opcode::ICmpSle:
      writer_.inst("bge", reg_name(RvReg::T1), reg_name(RvReg::T0),
                   true_copy_label);
      break;
    case Opcode::ICmpSge:
      writer_.inst("bge", reg_name(RvReg::T0), reg_name(RvReg::T1),
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
      emit_store_from_base(RvReg::T0, RvReg::Sp,
                           frame_.slot_offset(inst.get()), RvReg::T2);
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
    for (unsigned i = 0; i < inst.num_operands(); ++i) {
      if (i < arg_regs_.size()) {
        if (!load_i32(inst.operand(i), arg_regs_[i])) {
          return false;
        }
      } else {
        if (!load_i32(inst.operand(i), RvReg::T0)) {
          return false;
        }
        emit_store_from_base(RvReg::T0, RvReg::Sp,
                             static_cast<int>((i - 8) * 4), RvReg::T2);
      }
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
    for (unsigned i = 0; i < function_.params().size() && i < arg_regs_.size(); ++i) {
      emit_store_from_base(arg_regs_[i], RvReg::Sp,
                           frame_.register_param_offset(i), RvReg::T0);
    }
  }

  void emit_epilogue() {
    if (emitted_exit_) {
      return;
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
      auto reg = value_regs_.find(value);
      if (reg != value_regs_.end()) {
        emit_reg_copy(dst, reg->second);
        return true;
      }
      emit_load_from_base(dst, RvReg::Sp, frame_.slot_offset(value), dst);
      return true;
    }
    return fail("codegen cannot materialize value " + value->name());
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
    if (!frame_.has_slot(&inst)) {
      return fail("codegen has no result slot for " + inst.name());
    }
    auto reg = value_regs_.find(&inst);
    if (reg != value_regs_.end()) {
      emit_reg_copy(reg->second, src);
      return true;
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
      if (fits_i12(imm) && load_i32(lhs, RvReg::T0)) {
        emit_addi(RvReg::T2, RvReg::T0, imm);
        return spill_result(inst, RvReg::T2);
      }
      return false;
    }
    if (inst.opcode() == Opcode::Add && lhs->value_kind() == ValueKind::Constant) {
      const int imm = static_cast<const ConstantInt *>(lhs)->value();
      if (fits_i12(imm) && load_i32(rhs, RvReg::T0)) {
        emit_addi(RvReg::T2, RvReg::T0, imm);
        return spill_result(inst, RvReg::T2);
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
    return inst.parent() && inst.uses().size() == 1 &&
           dynamic_cast<const Instruction *>(inst.uses().front()) &&
           static_cast<const Instruction *>(inst.uses().front())->parent() == inst.parent() &&
           static_cast<const Instruction *>(inst.uses().front())->opcode() == Opcode::CondBr;
  }

  static bool is_icmp_opcode(Opcode opcode) {
    return opcode == Opcode::ICmpEq || opcode == Opcode::ICmpNe ||
           opcode == Opcode::ICmpSlt || opcode == Opcode::ICmpSgt ||
           opcode == Opcode::ICmpSle || opcode == Opcode::ICmpSge;
  }

  bool is_fallthrough(const BasicBlock &from, const BasicBlock &to) const {
    for (auto it = function_.blocks().begin(); it != function_.blocks().end(); ++it) {
      if (it->get() != &from) {
        continue;
      }
      ++it;
      return it != function_.blocks().end() && it->get() == &to;
    }
    return false;
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
        if (!is_register_candidate(*inst) || is_direct_branch_condition(*inst)) {
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
