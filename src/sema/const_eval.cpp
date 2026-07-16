#include "toyc/sema.h"

namespace toyc {

// AST expression constants are evaluated inside Sema because they need the active
// lexical scope and SemaResult side tables. Function calls are evaluated from SSA
// IR by the separate ConstEvaluator in src/ir/const_eval.cpp.

}  // namespace toyc
