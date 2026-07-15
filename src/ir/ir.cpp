#include "toyc/ir.h"

#include <sstream>

namespace toyc {

const char* type_name(Type type) {
    switch (type) {
        case Type::I32:   return "i32";
        case Type::Ptr:   return "ptr";
        case Type::Void:  return "void";
        case Type::Label: return "label";
    }
    return "?";
}

std::string Value::name() const {
    std::ostringstream os;
    if (kind_ == ValueKind::Param) {
        os << "%arg." << id_;
    } else {
        os << "%v." << id_;
    }
    return os.str();
}

void Value::add_use(User* user) { uses_.push_back(user); }

void Value::remove_use(User* user) {
    for (auto it = uses_.begin(); it != uses_.end(); ++it) {
        if (*it == user) {
            uses_.erase(it);
            return;
        }
    }
}

void User::add_operand(Value* value) {
    operands_.push_back(value);
    value->add_use(this);
}

void User::set_operand(unsigned i, Value* value) {
    Value* old = operands_[i];
    if (old == value) {
        return;
    }
    if (old) {
        old->remove_use(this);
    }
    operands_[i] = value;
    if (value) {
        value->add_use(this);
    }
}

void Value::replace_all_uses_with(Value* other) {
    if (other == this) {
        return;
    }
    std::vector<User*> users = uses_;
    for (User* user : users) {
        for (unsigned i = 0; i < user->num_operands(); ++i) {
            if (user->operand(i) == this) {
                user->set_operand(i, other);
            }
        }
    }
    uses_.clear();
}

const char* opcode_name(Opcode opcode) {
    switch (opcode) {
        case Opcode::Add: return "add";
        case Opcode::Sub: return "sub";
        case Opcode::Mul: return "mul";
        case Opcode::Sdiv: return "sdiv";
        case Opcode::Srem: return "srem";
        case Opcode::Neg: return "neg";
        case Opcode::ICmpEq: return "icmp eq";
        case Opcode::ICmpNe: return "icmp ne";
        case Opcode::ICmpSlt: return "icmp slt";
        case Opcode::ICmpSgt: return "icmp sgt";
        case Opcode::ICmpSle: return "icmp sle";
        case Opcode::ICmpSge: return "icmp sge";
        case Opcode::Alloca: return "alloca";
        case Opcode::Load: return "load";
        case Opcode::Store: return "store";
        case Opcode::Br: return "br";
        case Opcode::CondBr: return "cond_br";
        case Opcode::Ret: return "ret";
        case Opcode::Call: return "call";
        case Opcode::Phi: return "phi";
        case Opcode::Shl: return "shl";
        case Opcode::Shr: return "shr";
    }
    return "?";
}

void PhiInst::add_incoming(Value* value, BasicBlock* block) {
    add_operand(value);
    incoming_blocks_.push_back(block);
}

std::string BasicBlock::name() const {
    return id() == 0 ? "entry" : ("bb" + std::to_string(id()));
}

void BasicBlock::push_back(std::unique_ptr<Instruction> inst) {
    inst->set_parent(this);
    insts_.push_back(std::move(inst));
}

void BasicBlock::push_front(std::unique_ptr<Instruction> inst) {
    inst->set_parent(this);
    insts_.push_front(std::move(inst));
}

BasicBlock* Function::create_block() {
    unsigned id = block_counter_++;
    auto bb = std::make_unique<BasicBlock>(id, this);
    BasicBlock* raw = bb.get();
    blocks_.push_back(std::move(bb));
    return raw;
}

BrInst::BrInst(BasicBlock* target) : Instruction(Opcode::Br, Type::Void, 0) {
    add_operand(target);
}

CondBrInst::CondBrInst(Value* cond, BasicBlock* t, BasicBlock* f)
    : Instruction(Opcode::CondBr, Type::Void, 0) {
    add_operand(cond);
    add_operand(t);
    add_operand(f);
}

ConstantInt* Module::get_constant(int value) {
    auto it = constants_.find(value);
    if (it != constants_.end()) {
        return it->second.get();
    }
    auto owned = std::make_unique<ConstantInt>(value);
    ConstantInt* raw = owned.get();
    constants_.emplace(value, std::move(owned));
    return raw;
}

GlobalVar* Module::create_global(const std::string& name, int init_value, bool is_const) {
    auto addr = std::make_unique<GlobalAddr>(name);
    GlobalAddr* addr_raw = addr.get();
    global_addrs_.push_back(std::move(addr));

    auto gv = std::make_unique<GlobalVar>();
    gv->addr = addr_raw;
    gv->init = get_constant(init_value);
    gv->is_const = is_const;
    GlobalVar* gv_raw = gv.get();
    globals_.push_back(std::move(gv));
    return gv_raw;
}

Value* Module::create_register(Type type) {
    auto owned = std::make_unique<Value>(type, ValueKind::Register, fresh_id());
    Value* raw = owned.get();
    registers_.push_back(std::move(owned));
    return raw;
}

const Function* Module::find_function(const std::string& name) const {
    for (const std::unique_ptr<Function>& function : functions_) {
        if (function->short_name() == name) {
            return function.get();
        }
    }
    return nullptr;
}

}  // namespace toyc
