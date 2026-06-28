#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace toyc {

enum class Type { I32, Ptr, Void, Label };
const char* type_name(Type type);

enum class ValueKind { Constant, GlobalAddr, BasicBlock, Function, Register, Param };

class User;

class Value {
public:
    Value(Type type, ValueKind kind, unsigned id = 0)
        : type_(type), kind_(kind), id_(id) {}
    virtual ~Value() = default;

    Type type() const { return type_; }
    ValueKind value_kind() const { return kind_; }
    unsigned id() const { return id_; }
    void set_id(unsigned id) { id_ = id; }

    virtual std::string name() const;

    const std::vector<User*>& uses() const { return uses_; }
    void add_use(User* user);
    void remove_use(User* user);
    void replace_all_uses_with(Value* other);

protected:
    ValueKind kind() const { return kind_; }

private:
    Type type_;
    ValueKind kind_;
    unsigned id_ = 0;
    std::vector<User*> uses_;
};

class User : public Value {
public:
    using Value::Value;

    unsigned num_operands() const { return static_cast<unsigned>(operands_.size()); }
    Value* operand(unsigned i) const { return operands_[i]; }
    const std::vector<Value*>& operands() const { return operands_; }

    void add_operand(Value* value);
    void set_operand(unsigned i, Value* value);

protected:
    std::vector<Value*> operands_;
};

class ConstantInt : public Value {
public:
    explicit ConstantInt(int value) : Value(Type::I32, ValueKind::Constant, 0), value_(value) {}
    std::string name() const override { return std::to_string(value_); }
    int value() const { return value_; }
private:
    int value_;
};

class GlobalAddr : public Value {
public:
    explicit GlobalAddr(std::string label)
        : Value(Type::Ptr, ValueKind::GlobalAddr, 0), label_(std::move(label)) {}
    std::string name() const override { return "@" + label_; }
    const std::string& label() const { return label_; }
private:
    std::string label_;
};

struct GlobalVar {
    GlobalAddr* addr = nullptr;
    ConstantInt* init = nullptr;
    bool is_const = false;
};

class Function;  // forward
class Module;
class BasicBlock;  // forward

enum class Opcode {
    Add, Sub, Mul, Sdiv, Srem, Neg,
    ICmpEq, ICmpNe, ICmpSlt, ICmpSgt, ICmpSle, ICmpSge,
    Alloca, Load, Store,
    Br, CondBr, Ret, Call,
    Phi,
    Shl, Shr,  // introduced only by optimization (design §7/§8.6)
};
const char* opcode_name(Opcode opcode);

class Instruction : public User {
public:
    Instruction(Opcode opcode, Type type, unsigned id)
        : User(type, ValueKind::Register, id), opcode_(opcode) {}

    Opcode opcode() const { return opcode_; }
    BasicBlock* parent() const { return parent_; }
    void set_parent(BasicBlock* bb) { parent_ = bb; }

    bool is_terminator() const {
        return opcode_ == Opcode::Br || opcode_ == Opcode::CondBr || opcode_ == Opcode::Ret;
    }
    virtual bool has_result() const {
        return !(opcode_ == Opcode::Store || opcode_ == Opcode::Br ||
                 opcode_ == Opcode::CondBr || opcode_ == Opcode::Ret);
    }

private:
    Opcode opcode_;
    BasicBlock* parent_ = nullptr;
};

class BinaryInst : public Instruction {
public:
    BinaryInst(Opcode opcode, Value* lhs, Value* rhs, unsigned id)
        : Instruction(opcode, Type::I32, id) {
        add_operand(lhs);
        add_operand(rhs);
    }
};

class ICmpInst : public Instruction {
public:
    ICmpInst(Opcode opcode, Value* lhs, Value* rhs, unsigned id)
        : Instruction(opcode, Type::I32, id) {
        add_operand(lhs);
        add_operand(rhs);
    }
};

class NegInst : public Instruction {
public:
    NegInst(Value* operand, unsigned id) : Instruction(Opcode::Neg, Type::I32, id) {
        add_operand(operand);
    }
};

class AllocaInst : public Instruction {
public:
    explicit AllocaInst(unsigned id) : Instruction(Opcode::Alloca, Type::Ptr, id) {}
};

class LoadInst : public Instruction {
public:
    LoadInst(Value* ptr, unsigned id) : Instruction(Opcode::Load, Type::I32, id) {
        add_operand(ptr);
    }
};

class StoreInst : public Instruction {
public:
    // store <ptr>, <val>  (pointer first — design §4.5 decision)
    StoreInst(Value* ptr, Value* val) : Instruction(Opcode::Store, Type::Void, 0) {
        add_operand(ptr);
        add_operand(val);
    }
};

class BrInst : public Instruction {
public:
    explicit BrInst(BasicBlock* target);
};

class CondBrInst : public Instruction {
public:
    CondBrInst(Value* cond, BasicBlock* t, BasicBlock* f);
};

class RetInst : public Instruction {
public:
    explicit RetInst(Value* value = nullptr) : Instruction(Opcode::Ret, Type::Void, 0) {
        if (value) {
            add_operand(value);
        }
    }
};

class CallInst : public Instruction {
public:
    CallInst(std::string callee_name, std::vector<Value*> args, bool returns_void, unsigned id)
        : Instruction(Opcode::Call, returns_void ? Type::Void : Type::I32, id),
          callee_name_(std::move(callee_name)), returns_void_(returns_void) {
        for (Value* a : args) {
            add_operand(a);
        }
    }
    bool has_result() const override { return !returns_void_; }
    const std::string& callee_name() const { return callee_name_; }

private:
    std::string callee_name_;
    bool returns_void_;
};

class PhiInst : public Instruction {
public:
    explicit PhiInst(unsigned id) : Instruction(Opcode::Phi, Type::I32, id) {}
    void add_incoming(Value* value, BasicBlock* block);
    const std::vector<BasicBlock*>& incoming_blocks() const { return incoming_blocks_; }
    void replace_incoming_block(BasicBlock* old_block, BasicBlock* new_block) {
        for (BasicBlock*& block : incoming_blocks_) {
            if (block == old_block) {
                block = new_block;
            }
        }
    }

    // Reorder incoming (value,block) pairs to match pred_order. Used by mem2reg
    // so SSA textual output is byte-stable regardless of rename DFS visit order.
    void reorder_incoming(const std::vector<BasicBlock*>& pred_order) {
        std::vector<Value*> vals(pred_order.size(), nullptr);
        for (unsigned i = 0; i < incoming_blocks_.size(); ++i) {
            for (unsigned j = 0; j < pred_order.size(); ++j) {
                if (pred_order[j] == incoming_blocks_[i]) { vals[j] = operands_[i]; break; }
            }
        }
        for (unsigned i = 0; i < operands_.size(); ++i) {
            if (operands_[i]) operands_[i]->remove_use(this);
        }
        operands_.clear();
        incoming_blocks_.clear();
        for (unsigned j = 0; j < pred_order.size(); ++j) {
            add_incoming(vals[j], pred_order[j]);
        }
    }

private:
    std::vector<BasicBlock*> incoming_blocks_;
};

class ShlInst : public Instruction {
public:
    ShlInst(Value* value, unsigned amount, unsigned id)
        : Instruction(Opcode::Shl, Type::I32, id), amount_(amount) {
        add_operand(value);
    }
    unsigned amount() const { return amount_; }
private:
    unsigned amount_;
};

class ShrInst : public Instruction {
public:
    ShrInst(Value* value, unsigned amount, unsigned id)
        : Instruction(Opcode::Shr, Type::I32, id), amount_(amount) {
        add_operand(value);
    }
    unsigned amount() const { return amount_; }
private:
    unsigned amount_;
};

enum class FuncRet { Int, Void };

class BasicBlock : public Value {
public:
    explicit BasicBlock(unsigned id, Function* parent)
        : Value(Type::Label, ValueKind::BasicBlock, id), parent_(parent) {}

    std::string name() const override;
    Function* parent() const { return parent_; }

    void push_back(std::unique_ptr<Instruction> inst);
    void push_front(std::unique_ptr<Instruction> inst);
    std::list<std::unique_ptr<Instruction>>& insts() { return insts_; }
    const std::list<std::unique_ptr<Instruction>>& insts() const { return insts_; }

    Instruction* terminator() const { return insts_.empty() ? nullptr : insts_.back().get(); }
    bool is_terminated() const { const Instruction* t = terminator(); return t && t->is_terminator(); }

    const std::vector<BasicBlock*>& preds() const { return preds_; }
    const std::vector<BasicBlock*>& succs() const { return succs_; }
    void add_pred(BasicBlock* b) { preds_.push_back(b); }

private:
    Function* parent_;
    std::list<std::unique_ptr<Instruction>> insts_;
    std::vector<BasicBlock*> preds_;
    std::vector<BasicBlock*> succs_;
    friend class Function;
};

class Function : public Value {
public:
    Function(std::string name, FuncRet ret_type, unsigned param_count, Module* module)
        : Value(Type::Void, ValueKind::Function, 0),
          name_(std::move(name)), ret_type_(ret_type), module_(module) {
        for (unsigned i = 0; i < param_count; ++i) {
            auto p = std::make_unique<Value>(Type::I32, ValueKind::Param, i);
            params_.push_back(std::move(p));
        }
    }

    std::string name() const override { return "@" + name_; }
    const std::string& short_name() const { return name_; }
    FuncRet ret_type() const { return ret_type_; }
    Module* module() const { return module_; }

    const std::vector<std::unique_ptr<Value>>& params() const { return params_; }
    Value* param(unsigned i) const { return params_[i].get(); }

    BasicBlock* create_block();  // assigns block id; first created is entry
    BasicBlock* entry() const { return blocks_.empty() ? nullptr : blocks_.front().get(); }
    std::list<std::unique_ptr<BasicBlock>>& blocks() { return blocks_; }
    const std::list<std::unique_ptr<BasicBlock>>& blocks() const { return blocks_; }

private:
    std::string name_;
    FuncRet ret_type_;
    Module* module_;
    std::vector<std::unique_ptr<Value>> params_;
    std::list<std::unique_ptr<BasicBlock>> blocks_;
    unsigned block_counter_ = 0;
};

class Module {
public:
    Module() = default;

    ConstantInt* get_constant(int value);
    GlobalVar* create_global(const std::string& name, int init_value, bool is_const);

    unsigned fresh_id() { return value_counter_++; }
    Value* create_register(Type type);

    const std::vector<std::unique_ptr<GlobalVar>>& globals() const { return globals_; }

    Function* create_function(const std::string& name, FuncRet ret_type, unsigned param_count) {
        auto f = std::make_unique<Function>(name, ret_type, param_count, this);
        Function* raw = f.get();
        functions_.push_back(std::move(f));
        return raw;
    }
    const std::vector<std::unique_ptr<Function>>& functions() const { return functions_; }

private:
    std::unordered_map<int, std::unique_ptr<ConstantInt>> constants_;
    std::vector<std::unique_ptr<GlobalAddr>> global_addrs_;
    std::vector<std::unique_ptr<GlobalVar>> globals_;
    std::vector<std::unique_ptr<Value>> registers_;
    std::vector<std::unique_ptr<Function>> functions_;
    unsigned value_counter_ = 0;
};

}  // namespace toyc
