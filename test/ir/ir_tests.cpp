#include "toyc/ir.h"

#include "check.h"

using namespace toyc;

namespace {

// Local test-only User subclass: a 2-operand user to exercise use-list logic
// before real instructions exist. Declared in the test file, not in ir.h.
class TestUser : public User {
public:
    TestUser() : User(Type::Void, ValueKind::Register, 0) {
        operands_.resize(2, nullptr);
    }
};

void test_use_list_wiring() {
    Value a(Type::I32, ValueKind::Register, 0);
    Value b(Type::I32, ValueKind::Register, 1);
    TestUser u;
    u.set_operand(0, &a);
    u.set_operand(1, &b);

    toyc::test::check(u.num_operands() == 2, "user has 2 operands");
    toyc::test::check(u.operand(0) == &a && u.operand(1) == &b, "operands stored");
    toyc::test::check(a.uses().size() == 1 && a.uses()[0] == &u, "a used by u");
    toyc::test::check(b.uses().size() == 1 && b.uses()[0] == &u, "b used by u");

    u.set_operand(0, &b);
    toyc::test::check(a.uses().empty(), "a no longer used");
    toyc::test::check(b.uses().size() == 2, "b used twice");

    b.replace_all_uses_with(&a);
    toyc::test::check(u.operand(0) == &a && u.operand(1) == &a, "both operands now a");
    toyc::test::check(b.uses().empty(), "b fully replaced");
    toyc::test::check(a.uses().size() == 2, "a used twice after RAUW");
}

}  // namespace

int main() {
    test_use_list_wiring();
    return toyc::test::report();
}
