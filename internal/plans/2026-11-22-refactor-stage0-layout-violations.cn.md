# 0.5 演练：越层引用清单

**这一页是 stage 0.5 的第四件产物**，与 `docs/design/architecture/layout-map.cn.md`（映射表）、
`docs/design/architecture/layout-ownership.cn.md`（所有权表）、`scripts/layout-move.mjs`（读表、
生成 `git mv` 与包含改写、生成每块的构建目标）并列。

**它怎么来。** 脚本读映射表，在 develop 头上生成搬迁与包含改写，再按块重扫每一行 `#include`。
一处包含算越层，当且仅当它指向的文件落在**比它自己更深的块**里（层序见 `layout-map.cn.md`）。
演练结果**不合入**：下面这张表是读数，不是要提交的代码。

## 归属列：谁清掉这一条

- `0.2`——同一步 0.2 的重扫里清掉：本步引起的同块整理。
- `0.3`——由 0.3 清掉：`kernel/core/module/userdef.cpp`、`kernel/include/camel/core/module/userdef.h`、
  `kernel/runtime/definitions.cpp` 这几处对 `camel/parse`、`camel/common` 的引用。
- `0.4`——由 0.4 清掉：`kernel/include/camel/core/forward.h` 一处，`lib/transforms/` 五处
  （`jvp.cpp`、`module.cpp`、`rule_expand.cpp`、`rule_expand.h`、`vmap.cpp`）。
- `后续`——stage 0 的任何一步都不清，属于后面阶段某个 lane 的活：主要是 `unplaced` 里 `src/` 那一片
  的去向（U1–U9）。它不是"先放一放"，而是"这一次不问，问了就得替人做决定"。

## 读数

| 项 | 值 |
|---|---|
| 清单行数 | 226 |
| 其中越层引用 | 157（10 条同时带 stage-0 归属） |
| 两端同层、只是 stage-0 依赖倒置的 | 69 |
| 涉及文件 | 121 |
| 归不到位的项目头文件 | 0 |
| 需要改写的 `#include` 行 | 21 行 / 12 个文件 |
| 搬迁 | 80 项 / 39 个目录 |
| 块 | 8（`third_party`、`kernel_utils`、`kernel_core`、`kernel_runtime`、`parse`、`compile`、`lib_module`、`unplaced`） |

一行一个「文件 + 拼写」：同一文件里同一拼写出现两次只记一条；同一文件里两处不同拼写记两条。

## 这一页说不清的边界

1. **`lib_module` 是一个块。** 29 个模块之间的引用一律记成同块，块内谁依赖谁这一页看不到；
   要等模块各自成块（0.5 之后的内部工作）才看得见。
2. **`include/camel/common/` 留在 `unplaced`（U10）。** 于是任何 `X -> camel/common` 都必然被记成
   「伸进第 8 层」。这是 U10 未决造成的读数，不是这次搬家带进来的新引用。
3. **`unplaced` 是第 8 层（最上）。** `X -> unplaced` 的实际含义是「伸进还没搬走的 `src/`」；
   `src/` 的去向（U1–U9）定了之后，这些行会重新分布，数量会变。
4. **两处发布面的破坏由脚本自己补上，不在这一页。** `scripts/collect-out.js` 的 `stdlib` 平铺与
   `scripts/common.js` 的 `BUILD_INFO_ROOTS` 断在搬家上：前者要按 `lib/<模块>/` 重新拼，后者要加上
   四个新根。它们是「搬家必须顺手改」的一段，不是越层引用；后面重写发布面的步骤应把它们换成
   正式的来源，而不是留着这段补丁。
5. **清单不区分「同一个块里再分几层」。** 表只决定块，块内怎么切是后面各 lane 的活。

## 演练怎么算过

在 develop 头（`cdab42b4`，`origin/fsh-dev`）上应用整张表：`git mv` 80 项、`#include` 改写 21 行、
`cmake/layout-blocks.cmake` 15 个钩子；clang + ninja 从零全量 750 条边、`BUILD_EXIT=0`、192.7 秒
（主机一分钟负载 19.49）。在随后的 develop 头（`d2d6d369`）上重跑：生成、搬迁与改写逐项相同，
构建 0 条边待办（同一棵树已全绿）。编译能过靠的是一条**过渡包含路径**：`CAMEL_LAYOUT_TRANSITIONAL=ON`
时，每个块与每个模块/工具目标都追加 catch-all 根。把这一页清空、把那条过渡路径关掉，才是 0.5 之后的形状。

两次连续生成（7 个产物：`git-mv.txt`、`coverage.txt`、`violations.tsv`、`violations.md`、
`include-rewrites.tsv`、`unresolved.tsv`、`layout-blocks.cmake`）逐字节相同。

## 演练顺手查出来的四件事

这四件都不在清单里，因为清单只说"哪一处包含伸到了更深的块"，而这四处是**搬家把构建弄坏**的地方：
生成能过、语法能过，只有真编译才看得见。它们已经修在 `scripts/layout-move.mjs` 里，记在这里是因为
谁重写这段脚本都会再踩一次。

1. **块自己的目录里直接躺着的源文件没被收进库。** `file(GLOB_RECURSE _sources "dir/**/*.cpp")` 里的
   `**/` 是"再多一层"，不是"可以不多"：`kernel/runtime/definitions.cpp`、`parse/src/parse.cpp`、
   `kernel/utils/` 全部九个文件因此不在 `libcamel` 里，直到链接 `tiles-plan-check` 报
   `DefinitionStore::libraryFunction` 与 `CamelParser::CamelParser` 未定义。现在写 `dir/*.cpp`。
2. **catch-all 追加得太早会压掉工具自己加的头文件目录。** 工具在 `libcamel_tool.cmake` 之后才
   `target_include_directories(... ${CMAKE_CURRENT_SOURCE_DIR}/src ...)`，catch-all 若在钩子里当场追加，
   就排在它前面，`tools/debugger/server.cpp` 的 `#include "state.h"` 于是命中 `lib/state/include/state.h`。
   现在记下目标名，在 `add_subdirectory(tools)` 之后一次追加，排到最后。
3. **派生出的包含根不能落在 `include` 树内部。** `kernel/include/camel/utils` 一旦进了包含路径，
   libstdc++ 的 `<cassert>` 会顺着 `<assert.h>` 命中项目的 `camel/utils/assert.h`（它定义的是
   `ASSERT`，不是 `assert`），`kernel/core/mm/profiler.cpp` 于是报 `assert` 未声明。派生根现在丢掉
   任何落在 `include` 树里的路径。
4. **模块不并进 `libcamel`。** 顶层的 `LIBCAMEL_SRC` 一直只收 `src/**` 与 `third_party/**`；把 29 个
   模块的 88 个源文件再编一遍，既重复编译，又把只有某个模块才有的包含目录（pybind11、Python.h、
   onnxruntime 的 `.inc`）塞给整个库。模块仍由自己的 `<name>_cmo` 目标编译，`lib_module` 这个块只
   持有目录（清单与所有权表要用），不产出编译单元。
## 清单
| 归属 | 从 | 到 | 文件 | 拼写 |
|---|---|---|---|---|
| 0.2 | unplaced | unplaced | `src/execute/camel_pass.cpp` | `passes/sched/nodevm/nodevm.h` |
| 0.2 | unplaced | parse | `src/execute/pass_declarations.cpp` | `camel/parse/ast.h` |
| 0.2 | unplaced | parse | `src/execute/pass_declarations.cpp` | `camel/parse/parse.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/arm_loop_regions.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/bounds.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/broken_boundary.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/chunks.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/commit_checks.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/context_lifetime.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/contexts.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/control.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/deadwrite.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/derivatives.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/draft_erase_unread.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/draft_node_owner.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/edges.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/exec_plan.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/exhaust_perm_space.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/graph_faces.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/graph_release.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/inlineuse.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/keepfn.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/loop_collect.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/loop_inputs.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/loop_range.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/plan_refs.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/regions.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/retype.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/rules.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/saturate.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/tangent_space.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/throw_bad_alloc.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/undo_selfcheck.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/test/writings.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/trans/cml/cml.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `camel/passes/trans/json/gir.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/opt/defun/defunctionalize.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/opt/devirtualize/devirtualize.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/opt/generic/generic.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/opt/inline/inline.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/opt/opt/opt.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/opt/specialize/specialize.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/sched/fastvm/bcdump.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/sched/fastvm/fastvm.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/sched/fastvm/jit/dump/asmdump.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/sched/fastvm/jit/dump/bindump.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/sched/fastvm/jit/dump/mirdump.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/sched/nodevm/nodevm.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/sched/taskflow/taskflow.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/definitions.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/facts.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/fault.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/impl_body.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/index_semantics.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/native_call.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/resource_transition.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/resources.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/semantics.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/test/tags.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/trans/cpp/cpp_export.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/trans/dot/graphviz.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/trans/profile/profile.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/trans/rulesets/rulesets.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/trans/stats/stats.h` |
| 0.2 | unplaced | unplaced | `src/execute/pass.cpp` | `passes/trans/tns/topo_node_seq.h` |
| 0.3 | kernel_core | compile | `kernel/core/module/userdef.cpp` | `camel/compile/gir/encode.h` |
| 0.3 | kernel_core | parse | `kernel/core/module/userdef.cpp` | `camel/parse/parse.h` |
| 0.3 | kernel_core | compile | `kernel/core/module/userdef.cpp` | `compile/src/gct/builder.h` |
| 0.3 | kernel_core | compile | `kernel/core/module/userdef.cpp` | `compile/src/gir/builder.h` |
| 0.3 | kernel_core | compile | `kernel/include/camel/core/module/userdef.h` | `camel/compile/gct.h` |
| 0.3 | kernel_core | parse | `kernel/include/camel/core/module/userdef.h` | `camel/parse/parse.h` |
| 0.3 | kernel_runtime | compile | `kernel/runtime/definitions.cpp` | `camel/compile/gir/draft_graph_builder.h` |
| 0.3 | kernel_runtime | compile | `kernel/runtime/definitions.cpp` | `camel/compile/gir/encode.h` |
| 0.4 | kernel_core | parse | `kernel/include/camel/core/forward.h` | `camel/parse/ast/rule_head.h` |
| 0.4 | lib_module | parse | `lib/transforms/jvp.cpp` | `camel/parse/ast/rule_head.h` |
| 0.4 | lib_module | parse | `lib/transforms/module.cpp` | `camel/parse/ast/rule_head.h` |
| 0.4 | lib_module | parse | `lib/transforms/rule_expand.cpp` | `camel/parse/ast/rule_head.h` |
| 0.4 | lib_module | parse | `lib/transforms/rule_expand.h` | `camel/parse/ast/rule_head.h` |
| 0.4 | lib_module | parse | `lib/transforms/vmap.cpp` | `camel/parse/ast/rule_head.h` |
| 后续 | compile | unplaced | `compile/include/camel/compile/gct/tree.h` | `camel/common/ref.h` |
| 后续 | compile | unplaced | `compile/src/gct/builder.h` | `camel/common/scope.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_call.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_call.cpp` | `camel/execute/fault_isolation.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_calls.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_composition.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_composition.cpp` | `camel/execute/fault_isolation.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_control.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_decorators.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_diagnostics.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_enum.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_expr.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_generics.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_helpers.h` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_literal.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_modules.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_modules.cpp` | `camel/execute/executor.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_operator_writes.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_operator_writes.cpp` | `camel/execute/fault_isolation.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_operators.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_operators.cpp` | `camel/execute/fault_isolation.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_ssa.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_types.h` | `camel/common/ref.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_types.h` | `camel/common/scope.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_variables.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder_with.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/builder.cpp` | `camel/execute/entry_args.h` |
| 后续 | compile | unplaced | `compile/src/gir/ssa_env.h` | `camel/common/scope.h` |
| 后续 | compile | unplaced | `compile/src/init.h` | `camel/common/scope.h` |
| 后续 | kernel_core | unplaced | `kernel/core/context/context.cpp` | `camel/execute/executor.h` |
| 后续 | kernel_core | unplaced | `kernel/core/mm/block.cpp` | `camel/profile/allocation.h` |
| 后续 | kernel_core | unplaced | `kernel/core/mm/gen.cpp` | `camel/profile/allocation.h` |
| 后续 | kernel_core | unplaced | `kernel/core/mm/growable.cpp` | `camel/profile/allocation.h` |
| 后续 | kernel_core | unplaced | `kernel/core/module/builtin.cpp` | `builtin/modules/globals.h` |
| 后续 | kernel_core | unplaced | `kernel/core/module/builtin.cpp` | `builtin/modules/profiler.h` |
| 后续 | kernel_core | unplaced | `kernel/core/module/builtin.cpp` | `builtin/modules/test_attrs.h` |
| 后续 | kernel_core | unplaced | `kernel/core/rtdata/array.cpp` | `camel/profile/profile.h` |
| 后续 | kernel_core | unplaced | `kernel/core/rtdata/variant.cpp` | `camel/profile/profile.h` |
| 后续 | kernel_core | unplaced | `kernel/core/type/enum.cpp` | `camel/common/ref.h` |
| 后续 | kernel_core | unplaced | `kernel/include/camel/core/module/module.h` | `camel/common/ns.h` |
| 后续 | kernel_core | unplaced | `kernel/include/camel/core/tags.h` | `camel/common/ref.h` |
| 后续 | kernel_runtime | unplaced | `kernel/runtime/rules.cpp` | `camel/execute/executor.h` |
| 后续 | kernel_runtime | unplaced | `kernel/runtime/rules.cpp` | `camel/execute/graph_runtime_support.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/assert.cpp` | `camel/utils/assert.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/log_format.cpp` | `camel/utils/log_format.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/log.cpp` | `camel/utils/log.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/memperf.cpp` | `camel/utils/memperf.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/once.cpp` | `camel/utils/once.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/phase_timer.cpp` | `camel/utils/env.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/phase_timer.cpp` | `camel/utils/path.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/phase_timer.cpp` | `camel/utils/phase_timer.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/scope.cpp` | `camel/utils/scope.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/sha256.cpp` | `camel/utils/sha256.h` |
| 后续 | kernel_utils | kernel_core | `kernel/utils/strpt.cpp` | `camel/utils/strpt.h` |
| 后续 | lib_module | unplaced | `lib/autodiff/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/autodiff/pullback.cpp` | `camel/passes/opt/inline.h` |
| 后续 | lib_module | unplaced | `lib/bytes/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/ckpt/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/env/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/env/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/fs/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/fs/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/graph/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/graph/module.cpp` | `camel/execute/pass/base.h` |
| 后续 | lib_module | unplaced | `lib/graph/probe.cpp` | `camel/execute/pass/base.h` |
| 后续 | lib_module | unplaced | `lib/graph/probe.cpp` | `camel/execute/pass/params.h` |
| 后续 | lib_module | unplaced | `lib/graph/read.cpp` | `camel/passes/opt/constant_bytes.h` |
| 后续 | lib_module | unplaced | `lib/graph/txn.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/graph/txn.cpp` | `camel/execute/pass/warnings.h` |
| 后续 | lib_module | unplaced | `lib/graph/txn.cpp` | `src/passes/opt/generic/rewrite.h` |
| 后续 | lib_module | unplaced | `lib/hash/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/hash/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/io/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/io/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/json/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/json/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/math/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/math/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/nn/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/onnx/exporter.cpp` | `camel/execute/entry_args.h` |
| 后续 | lib_module | unplaced | `lib/onnx/exporter.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/onnx/exporter.cpp` | `camel/execute/graph_runtime_support.h` |
| 后续 | lib_module | unplaced | `lib/onnx/exporter.cpp` | `camel/passes/macro/macro.h` |
| 后续 | lib_module | unplaced | `lib/onnx/exporter.cpp` | `camel/passes/opt/config.h` |
| 后续 | lib_module | unplaced | `lib/onnx/exporter.cpp` | `camel/passes/opt/generic.h` |
| 后续 | lib_module | unplaced | `lib/onnx/exporter.cpp` | `camel/passes/trans/dot/graphviz.h` |
| 后续 | lib_module | unplaced | `lib/onnx/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/onnx/passes/export.cpp` | `camel/execute/entry_args.h` |
| 后续 | lib_module | unplaced | `lib/onnx/passes/export.cpp` | `camel/execute/graph_runtime_support.h` |
| 后续 | lib_module | unplaced | `lib/onnx/passes/export.cpp` | `camel/execute/pass/params.h` |
| 后续 | lib_module | unplaced | `lib/onnx/passes/export.cpp` | `camel/passes/opt/config.h` |
| 后续 | lib_module | unplaced | `lib/onnx/passes/export.h` | `camel/execute/pass/trans.h` |
| 后续 | lib_module | unplaced | `lib/process/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/process/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/pyplot/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/pyplot/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/python/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/python/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/random/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/random/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/re/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/re/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/rewrite/fact.cpp` | `camel/execute/graph_runtime_support.h` |
| 后续 | lib_module | unplaced | `lib/rewrite/fact.cpp` | `src/passes/sched/nodevm/nodevm.h` |
| 后续 | lib_module | unplaced | `lib/rewrite/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/rewrite/module.cpp` | `src/passes/sched/nodevm/nodevm.h` |
| 后续 | lib_module | unplaced | `lib/sim/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/sim/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/state/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/state/module.cpp` | `camel/execute/function_call.h` |
| 后续 | lib_module | unplaced | `lib/str/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/str/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/tensor/fusion_group.cpp` | `camel/execute/fusion_group.h` |
| 后续 | lib_module | unplaced | `lib/tensor/kernels/cpu.cpp` | `camel/execute/cpu_isa.h` |
| 后续 | lib_module | unplaced | `lib/tensor/module.cpp` | `camel/execute/entry_args.h` |
| 后续 | lib_module | unplaced | `lib/tensor/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/tensor/ops/op_stats.cpp` | `camel/profile/profile.h` |
| 后续 | lib_module | unplaced | `lib/tensor/passes/fuse.cpp` | `camel/execute/pass/params.h` |
| 后续 | lib_module | unplaced | `lib/tensor/passes/fuse.h` | `camel/execute/pass/opt.h` |
| 后续 | lib_module | unplaced | `lib/tensor/passes/index_eval.h` | `camel/execute/pass/opt.h` |
| 后续 | lib_module | unplaced | `lib/test_support/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/test_support/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/this/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/this/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/this/operators.cpp` | `ZEN` |
| 后续 | lib_module | unplaced | `lib/tiles/include/tiles/internal/tree_transfer.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/capability.cpp` | `camel/execute/devices.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/census.cpp` | `camel/execute/pass/opt.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/compiler.cpp` | `camel/execute/graph_runtime_support.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/cuda_backend.cpp` | `camel/execute/devices.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/module.cpp` | `camel/execute/devices.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/module.cpp` | `camel/execute/function_call.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/module.cpp` | `camel/profile/profile.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/region_pass.cpp` | `camel/execute/pass/opt.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/region_pass.cpp` | `camel/passes/opt/generic.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/region_test_pass.cpp` | `camel/execute/function_call.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/region_test_pass.cpp` | `camel/execute/pass/opt.h` |
| 后续 | lib_module | unplaced | `lib/tiles/src/region_test_pass.cpp` | `camel/execute/pass/params.h` |
| 后续 | lib_module | unplaced | `lib/time/executor.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/time/executor.h` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/transforms/module.cpp` | `camel/execute/executor.h` |
| 后续 | lib_module | unplaced | `lib/transforms/module.cpp` | `camel/execute/pass/opt.h` |
| 后续 | lib_module | unplaced | `lib/transforms/module.cpp` | `camel/execute/pass/params.h` |
| 后续 | parse | unplaced | `parse/include/camel/parse/ast/base.h` | `camel/common/ref.h` |
| 后续 | parse | unplaced | `parse/include/camel/parse/ast/base.h` | `camel/common/tree.h` |
| 后续 | parse | unplaced | `parse/include/camel/parse/ast/other.h` | `camel/common/ref.h` |

