#pragma once

namespace toyc {

class Module;
class Function;

// Each pass is a pure IR->IR function returning whether it changed anything.
// They run only under -opt (see run_optim). All operate per-function.
bool constprop(Function& fn);
// Sparse conditional constant propagation over the existing SSA form.  This
// also folds constant branches and cleans up newly unreachable CFG regions.
bool sccp(Function& fn);
bool algebraic_simplify(Function& fn);
bool strength_reduce(Function& fn);
bool licm(Function& fn);
bool dce(Function& fn);
bool gvn(Function& fn);
bool cfs(Function& fn);
bool eliminate_tail_recursion(Function& fn);
bool limited_inline(Module& module);
// Promote globals through calls only when a transitive module-level mod/ref
// summary proves that the callees cannot observe the promoted object.
bool interprocedural_global_opt(Module& module);
bool localize_globals(Module& module);

// Drives the four passes to a fixpoint over every function (design §9).
bool run_optim(Module& module);

}  // namespace toyc
