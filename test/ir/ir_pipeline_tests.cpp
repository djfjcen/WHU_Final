#include "test/support/ir_fixture.h"

#include <gtest/gtest.h>

namespace toyc {
namespace {

using toyc::test::compile_ir;
using toyc::test::IrLevel;

// ---------- mem2reg ----------

TEST(Mem2Reg_Pipeline, IfElse) {
    const char* src = R"(int main() {
  int a = 0;
  if (1) { a = 1; } else { a = 2; }
  return a;
}
)";
    const char* expected = R"(define i32 @main() {
entry:
  cond_br 1, label bb1, label bb2
bb1:
  br label bb3
bb2:
  br label bb3
bb3:
  %v.2 = phi [1, bb1], [2, bb2]
  ret %v.2
}
)";
    EXPECT_EQ(expected, compile_ir(src, IrLevel::Mem2Reg));
}

TEST(Mem2Reg_Pipeline, Loop) {
    const char* src = R"(int main() {
  int i = 0;
  while (i < 2) { i = i + 1; }
  return i;
}
)";
    const char* expected = R"(define i32 @main() {
entry:
  br label bb1
bb1:
  %v.6 = phi [0, entry], [%v.4, bb2]
  %v.2 = icmp slt %v.6, 2
  cond_br %v.2, label bb2, label bb3
bb2:
  %v.4 = add %v.6, 1
  br label bb1
bb3:
  ret %v.6
}
)";
    EXPECT_EQ(expected, compile_ir(src, IrLevel::Mem2Reg));
}

// ---------- optim (mem2reg + all passes) ----------

TEST(Optim_Pipeline, ConstFold) {
    const char* src = R"(int main() {
  return 2 + 3 * 4;
}
)";
    const char* expected = R"(define i32 @main() {
entry:
  ret 14
}
)";
    EXPECT_EQ(expected, compile_ir(src, IrLevel::Optim));
}

TEST(Optim_Pipeline, DeadCode) {
    const char* src = R"(int main() {
  int x = 5;
  int y = x + 1;
  return 3;
}
)";
    const char* expected = R"(define i32 @main() {
entry:
  ret 3
}
)";
    EXPECT_EQ(expected, compile_ir(src, IrLevel::Optim));
}

TEST(Optim_Pipeline, Cse) {
    const char* src = R"(int f(int a, int b) {
  int x = a + b;
  int y = a + b;
  return x + y;
}

int main() {
  return f(1, 2);
}
)";
    const char* expected = R"(define i32 @f(i32 %arg.0, i32 %arg.1) {
entry:
  %v.5 = add %arg.0, %arg.1
  %v.12 = add %v.5, %v.5
  ret %v.12
}
define i32 @main() {
entry:
  %v.13 = call @f, 1, 2
  ret %v.13
}
)";
    EXPECT_EQ(expected, compile_ir(src, IrLevel::Optim));
}

TEST(Optim_Pipeline, Cfs) {
    const char* src = R"(int main() {
  int a = 0;
  if (1) { a = 7; } else { a = 8; }
  return a;
}
)";
    const char* expected = R"(define i32 @main() {
entry:
  ret 7
}
)";
    EXPECT_EQ(expected, compile_ir(src, IrLevel::Optim));
}

}  // namespace
}  // namespace toyc
