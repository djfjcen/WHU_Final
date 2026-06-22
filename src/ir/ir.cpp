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

}  // namespace toyc
