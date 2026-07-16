#pragma once

namespace toyc {

class Module;

// Inline small, non-recursive (leaf) int-returning functions into their call
// sites. Runs before mem2reg on the pre-SSA IR (allocas + load/store, no phis),
// so cloned locals are promoted together with the caller afterwards. Returns
// whether any call site was inlined. Only used under -opt.
bool inline_functions(Module& module);

}  // namespace toyc
