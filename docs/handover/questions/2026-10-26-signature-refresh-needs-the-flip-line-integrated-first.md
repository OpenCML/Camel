# 把签名区改动刷新到今天 develop：要定的不是签名怎么写，而是合并落在哪棵树上

> 说明（提出者）：读数取自本机检出 `D:\Projects\Camel`（`origin/develop` = `f5c55673b`，2026-10-26 01:28）
> 与工作树 `D:\Projects\Camel-wt\h593-stdlib-migration-refresh`（分支 `handover/h593-stdlib-migration-refresh`，
> 基点 `handover/h593-stdlib-migration` = `57558da3a`）。一切用 `git`/`git merge-tree` 在本地取的读数都在下面逐字给出；
> 合并试过一次后已 `git merge --abort`，分支仍停在 `57558da3a`。

## 一、是什么

任务是把「标准库签名区不再写已废的类型名 `Tensor`」这笔改动（`handover/h593-stdlib-migration`，tip `57558da3a`）
刷新到今天的 `origin/develop`，做法是**以那条旧分支为基点**、在工作树里 `git merge --no-ff origin/develop`，
冲突「签名区取元素类型那一侧、生成物一律脚本重出、`docs/reference/camel-for-ai.md` 取 develop 侧」。

问题在于基点：`handover/h593-stdlib-migration` **不是「develop + 1 个提交」，而是翻转车道 `h465-flip-integration`
一系的尖端再叠 1 个提交**。所以「合 develop」实际是**两条并行车道的一次互并**，不是一次刷新。
这次互并里有 4 处是两条车道对同一段代码的两种设计，不是文本相邻造成的可两侧同留的冲突；
它们全在被允许文件集之外（`src/**`、`modules/**`、`include/**`），而且裁定权不在本 lane。

## 二、逐字证据

（1）基点关系：

    $ git merge-base origin/develop handover/h593-stdlib-migration
    72d148822a1c0e31697e18bd4c0bf7cafc61ae62          # 2026-10-25 12:56
    $ git rev-list --count origin/develop..handover/h593-stdlib-migration   -> 363
    $ git rev-list --count handover/h593-stdlib-migration..origin/develop   -> 361
    $ git diff --shortstat 72d148822a origin/develop                        -> 597 files, 19101 insertions(+), 5754 deletions(-)
    $ git diff --shortstat 72d148822a handover/h593-stdlib-migration        -> 580 files, 14082 insertions(+), 5481 deletions(-)

`57558da3a` 自己只动 5 个文件（`stdlib/signatures/nn.cml` 34、`stdlib/signatures/tensor.cml` 110、
`stdlib/types.cml` 2、`docs/stdlib-reference.md`、`docs/reference/camel-for-ai.md`），但它的父 `3980cbb55`
是翻转车道提交。

（2）按任务书合并的结果（`git merge --no-ff origin/develop`，**34 个文件冲突**）：

    benchmarks/resnetbn/tools/gen_model_cml.py   diagnostics/registry.tsv            docs/diagnostics.md
    docs/handover/questions/tensor-reduction-element-type-code-half.md                docs/language/{03_data_model,06_expressions,07_statements}.md
    docs/language/en/{03_data_model,06_expressions,07_statements}.md                  docs/reference/camel-for-ai.md
    export_matrix.md                             include/camel/core/error/diagnostics/messages/semantic.h
    modules/tensor/kernels/elementwise.{cpp,h}   modules/tensor/ops/layout_ops.cpp
    modules/tiles/include/tiles/internal/regions.h                                    modules/tiles/src/{graph_regions,regions}.cpp
    scripts/gen_export_matrix.py                 src/builtin/modules/globals.cpp       src/builtin/operators/ops.cpp
    src/core/error/diagnostics/messages/semantic.cpp                                  src/core/rtdata/array.cpp
    stdlib/signatures/nn.cml                     stdlib/signatures/tensor.cml
    test/cases/modules/{nn/mlp.cml,onnx/export_train_step_vjp.cml,tensor/numeric_contract.cml,tiles/kernel_ir_check.cpp}
    test/plans/feat/compile/array_alias.plan.toml   test/plans/feat/modules/{autodiff,rules,tensor/dtype}.plan.toml

允许文件集内的只有 7 个：`stdlib/signatures/{nn,tensor}.cml`、`docs/reference/camel-for-ai.md`（取 develop 侧）、
`docs/handover/questions/**`，加上三个生成物 `diagnostics/registry.tsv`、`docs/diagnostics.md`、`export_matrix.md`
（构建/脚本重出）；**其余 27 个在允许文件集之外**。4 处真正的语义分歧（两侧改写同一段，不能同留）：

* `modules/tensor/ops/layout_ops.cpp`：slice 节点的操作数布局不同。
  develop 侧 `sliceSubKernel` 读 `(input, start=1, end=2, step=3, written=4)`；
  车道侧读 `(input, axis=1, start=2, end=3, step=4, written=5)`，并把轴写成常量 0 的检查
  （`if (intArg(norm, 1) != 0) reportShapeError(...)`）。同一处 VJP 也跟着分叉：
  develop `requireVjpInputs(call, 5, 5)` / 车道 `requireVjpInputs(call, 4, 6)`。
* `src/builtin/operators/ops.cpp`：写变量算什么值。develop 侧 `return rhs;` / `return value;`；
  车道侧 `cell->setHoldsUnsharedArray(false); return toSlot(cell);`（写给出的是变量的版本）。
* `modules/tensor/kernels/elementwise.{h,cpp}`：bf16/f16 的槽位表示。develop 侧保留
  `rtdata::BFloat16 bf16; rtdata::Float16 f16;`（带自己的构造函数 `Value() : bf16() {}`）；
  车道侧 `uint16_t u16;` 存两字节位型。
* `modules/tiles/**` 与 `regions.h`：develop 的 `partitionRegions -> std::vector<Segment>` 加
  `RegionTransfers planRegionTransfers(...)`；车道已改名 `Segment -> Region` 并换成 `planResidency(...)`
  （`test/cases/modules/tiles/kernel_ir_check.cpp` 两侧因此对不上）。

其余是可「两侧都留」的插入型冲突。例如诊断枚举
`include/camel/core/error/diagnostics/messages/semantic.h:410`：develop 加
`RuleConstParamNotMatched = 0x0001E8` / `RuleWithParameterUnbound = 0x0001DD` / `RuleWithArgumentNotConstant = 0x0001F0`，
车道加 `TensorElementTypeRequired = 0x0001ED` / `BranchElementTypeMismatch = 0x0001EE`——**编号不撞**，
只是插在同一处；`diagnostics/registry.tsv`、`docs/diagnostics.md`、`export_matrix.md` 同理，重跑生成物即可。
但这些文件也不在允许集内。

（3）唯一符合任务书预期形状的树是 `handover/h588-flip-merge-develop`（合的是 develop 的 `38e44baf6`）：

    $ git merge-tree --write-tree --name-only handover/h588-flip-merge-develop handover/h593-stdlib-migration
    a3ae0a0003fbe6f0196b3428f3f211eaaf32a55c
    stdlib/signatures/nn.cml
    stdlib/signatures/tensor.cml                       # 生成物自动合并

即「冲突只落在签名文件」对应的是**翻转车道已并入 develop 的那棵树**，而不是 `h593` 本身。
而 `h588` 已落后今天 develop **40 个提交**（缺 `1a66b86d5`、`91003b3a8` 那笔「附录移出必读页」，
以及 `ed3133432`、`bafacff0a`、`f5c55673b`），`git merge-tree h588 origin/develop` 仍有 6 个文件冲突
（`benchmarks/resnetbn/tools/gen_model_cml.py`、`docs/handover/questions/tensor-reduction-element-type-code-half.md`、
`docs/reference/camel-for-ai.md`、`scripts/gen_export_matrix.py`、`src/core/rtdata/array.cpp`、
`test/plans/feat/modules/rules.plan.toml`）。

## 三、今天的读数

**这笔改动今天仍然需要**（develop 上签名区仍写着 `Tensor`）：

    $ git grep -c 'Tensor' origin/develop -- stdlib/signatures/nn.cml stdlib/signatures/tensor.cml stdlib/types.cml
    origin/develop:stdlib/signatures/nn.cml:17          # 17 行 / 62 处（-o 计数）
    origin/develop:stdlib/signatures/tensor.cml:60      # 60 行 / 122 处
    （types.cml 无匹配）

分支尖端：

    $ git grep -c 'Tensor' handover/h593-stdlib-migration -- stdlib/signatures/nn.cml stdlib/signatures/tensor.cml stdlib/types.cml
    handover/h593-stdlib-migration:stdlib/signatures/tensor.cml:7    # 7 行 / 8 处，全部合法：
        tensor.cml:59  export func range(start: int, stop?: int, step?: int): Tensor<int64>
        tensor.cml:7   `TensorLike`、`TensorOrScalar` 是散文里的名字
        tensor.cml:322/331/338/399  `TensorDimensionMismatch` 是故障名
    （nn.cml 与 types.cml 无匹配）
    # stdlib/types.cml 那处（is_plain::<(x: int, y: f32)>() -> is_plain::<{ x: int, y: f32 }>()）
    # 在今天 develop 上**不存在**：develop 的 stdlib/types.cml 是 94 行、与 merge-base 逐字相同、没有 is_plain；
    # 车道的 types.cml 是 123 行，is_plain 是翻转车道新加的函数，这一处只对车道内部成立。

基线树（`57558da3a`）上的静态读数（本机 node，源码检查，不构建）：

    $ node scripts/check-doc-links.mjs --all        -> no broken links in 251 document(s)        exit 0
    $ node scripts/check-doc-links.mjs --citations  -> 413 of 413 symbol citation(s) verified     exit 0
    $ node scripts/camel-for-ai.mjs --check         -> the document is 112343 characters; its budget is 112000
                                                       1 problem(s) in docs/reference/camel-for-ai.md   exit 1

最后一条是**基线的既有红**：基点是「附录还在必读页里」的旧版布局（`docs/reference/camel-for-ai.md` 2147 行、
只有这一个文件），而 develop 已把它拆成三份（`camel-for-ai.md` 1067 行 + `camel-for-ai-diagnostics.md`
+ `camel-for-ai-stdlib-signatures.md`），生成器也从单文档 `BUDGET` 改成多文档 `LAYOUT`。
也就是说：**分支里对 `docs/reference/camel-for-ai.md` 的那笔改动已经整体作废**，签名的新拼写只可能出现在
develop 的 `docs/reference/camel-for-ai-stdlib-signatures.md` 里。

## 四、选项与代价

* **A（按原计划的精神，换基点）**：先把今天的 develop 并进翻转车道（`h588` 那件事在 `f5c55673b` 上重做一次，
  冲突 6 个文件起），在这棵「车道 × develop」的树上重做签名改动（冲突只剩 `stdlib/signatures/{nn,tensor}.cml`）。
  代价：这一步动 `src/**`、`modules/**`、`include/**`，属于协调者与车道所有者的整合，不在本 lane 的允许集内；
  4 处语义分歧仍要在那棵树上一一裁定（`h588` 已经裁过一次，但它的 develop 侧已过期）。
* **B（在今天 develop 上直接重做签名改动）**：新开一条以 `origin/develop` 为基点的分支，只改
  `stdlib/signatures/{nn,tensor}.cml`（`types.cml` 无此点）、重出 `docs/stdlib-reference.md` 与
  `docs/reference/camel-for-ai-stdlib-signatures.md`。代价：**改动比车道上的版本小**——`Tensor` 的废除
  与 element type 的强制都在翻转车道上，develop 还没有；`registry.docs_complete` 那条红出在车道上，
  在 develop 上这笔改动是否真能把一条红变绿，必须先量（本 lane 未做此改动，故无读数）。
* **C（什么都不刷新）**：签名改动留在翻转车道上，等车道并入 develop 时随车一起落。
  代价：`h593` 这条分支继续挂在车道上，作者点头后无法单独落。

需要协调者/作者给的裁定只有两个：(i) 走 A、B 还是 C；(ii) 走 A 时上面 4 处（slice 节点操作数布局、
写变量的返回值、bf16/f16 槽位、tiles 的 `Segment`/`Region` 与 `planRegionTransfers`/`planResidency`）
各取哪一侧——本 lane 不自行裁定，也不改被禁止的路径。