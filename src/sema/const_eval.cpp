#include "toyc/sema.h"

namespace toyc {

// AST expression constants are evaluated inside Sema because they need the active
// lexical scope and SemaResult side tables. Per the ToyC spec, const initializers
// only contain numeric literals, already-declared consts, and their arithmetic /
// logical combinations, so no whole-program interpretation is involved.

}  // namespace toyc
