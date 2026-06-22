#pragma once

#include <cstdint>
#include <ostream>
#include <string>
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

}  // namespace toyc
