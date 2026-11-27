# `opt::inline`：用 Camel 写的内联

负责人 2026-11-27 定：CC 稿 Figure 1 的流水线是 `f->opt::inline->opt::cse->opt::algebra`，三步各有可见的活。`opt` 模块里已有 `fold`、`cse`、`dce`、`algebra`、`saturate`，内联还只有 C++ 的 `std::cxx::inline`。按 CC 格的优先级做。

## 1. 图算子：在调用处展开被调函数

`graph` 模块现有的 `inline(at, fn)` 只展开不带参数、不捕获变量的函数值。内联一个调用需要的是：在调用节点处展开它的被调函数，形参绑到这个调用的实参上。加一个内核算子（建议库里写成 `inline` 的单参重载 `inline(at: Node) => Node`，URI 另起，如 `graph:inline_call`）：

- `at` 是一个调用节点，被调的图由 `callee(at)` 那条规则确定；交回被调函数体的值在调用处的那个节点。
- 位置规则与 `inline(at, fn)` 相同：调用在它所在块的顺序链上的位置交给展开出的代码，调用本身随之消失。
- 被调函数捕获的值照它的函数值带进来（与形参同样绑定）；被调的图不确定（经形参或正在组装的闭包调用）、或调用的就是正在编辑的这张图（递归）时，按算子的 `@fails` 报，不猜。
- 它是图内核的一个操作，不是遍；建在现有展开机制上，不另起一套复制逻辑。

## 2. 遍：`opt::inline`

`with <growth_budget: int = 4096> export func inline::<F>(f: F) => F! when is_function::<F>()`，与 `std::cxx::inline` 同名同参：

- 规模与 `opt::fold` 相当：遍历 `f` 可达的各图中的调用，按 `std::cxx::inline` 的选择规则（哪些调用可内联、什么顺序、增长预算怎么计）逐个调用第 1 节的算子，`commit`。
- 选择规则从 `src/stdlib/std/cxx/opt/inline/engine.cpp` 里"只内联"那一路读出来照写，不另行设计；读的过程中发现 C++ 那边的规则说不清的，单列报决策者，不在 Camel 这边替它定。
- 模块文件头的清单与"两套实现比对"那段把 `inline` 加上。

## 3. 比对

照 `test/plans/feat/opt/cxx_isolation.plan.toml` 里 `fold`、`cse` 的样子加一对 `inline.by_cpp` / `inline.by_camel`：同一份语料，`std::jgir canonical=true` 读两边的图，逐字节相等。语料至少包括：Figure 1 的例子（`inc(x) * 3 + (x + 1) * 4`）、一个递归函数（不展开）、一个捕获变量的闭包调用、一个顶到增长预算的例子（各 0 时一个都不展开）。只有"两边都没动"也会逐字节相等，所以每条语料另断言内联确实发生了（调用节点数减少）或确实没有发生（递归那条）。

## 4. Figure 1 读数

读数页（`internal/reports/` 下 Figure 1 那一页）现在用 `std::cxx::inline` 量；`opt::inline` 落地后用 `f->opt::inline->opt::cse->opt::algebra` 再量一次，并与 `std::cxx::inline` 那一版逐字节比。

## 验收

- 第 3 节那对用例在尖端上绿，且断言"内联确实发生"成立。
- `opt::inline` 的 `@example` 在库示例检查里编得过。
- Figure 1 读数页有 `opt::inline` 那一版的三张图，结尾是 `x → (+1) → (*7)`。
- 新增红 0。
