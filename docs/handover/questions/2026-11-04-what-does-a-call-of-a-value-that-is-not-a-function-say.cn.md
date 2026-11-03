# 调用一个类型不是函数的值，诊断该怎么写？

**状态**：待裁；我方默认见第 4 节（**复用现有 `ArgumentsRejected`，把真话写在它的自由文本槽里**），
本轮已按默认落地。

## 1. 问句

**「一个值被当函数调用、而它的类型不是函数——`let t = (1.0, 2.0); t(1.0)`，或
`vjp(f)(3.0)(1.0)`——报告该用它自己的码（`diagnostics/registry.tsv` 里下一个 error 号），还是复用
已有的 `SemanticDiag::ArgumentsRejected`？」**

## 2. 让它成为问句的读数

树 `D:\Projects\Camel-wt\linkfn` @ 实时 `origin/develop`（`34b659107`）+ 本车道改动，宿主 88zjwei
（32 核），2026-11-04 05:5x–06:1x +08:00，1 m load **13.3 / 19.5 / 22.2**（满载区间：数字只用于判形态）。

**这是编译器缺陷，不是程序的错。** 基线树 `34b659107` 上，五种"调用一个不是函数的值"全部
`exit 134`，`Camel Invariant Violation`，位置 `src/compile/gir/builder_operators.cpp#Builder::visitLinkNode`、
判据 `dataType->code() == TypeCode::Function`：

| 形状 | 值类型 | 基线 |
| --- | --- | --- |
| `let k = 1.0 * 2.0; k(1.0)` | `float` | exit 134 |
| `let t = (1.0, 2.0); t(1.0)` | `(float, float)` | exit 134 |
| `let t = (1.0,); t(1.0)` | `(float,)` | exit 134 |
| `let p = pair(1.0); p(2.0)`（`pair` 返回二元组） | `(float, float)` | exit 134 |
| `vjp(f)(3.0)(1.0)`（算子的**答案元组**被调用） | `(float, (float) => float)` | exit 134 |

也就是说：`visitLinkNode` 的判据把**程序输入**当成了"建图器自己的不变量被违反"。同一条判据下，
`536999073` 已经为 `T!` 开出 `CalledFallibleValue` 并明说"`GUARD` 停的是进程，不是程序"，剩下
"类型不是函数、也不是 `T!`"这一类至今没有出口。

**与 `transpose` 无关的一半**：上面五条一行 `transpose` 都没有。
**与 `transpose` 有关的一半**：反过来量，基线树上 `let back = transpose(read_outer); back(1.0, 1.0)`
与 `transpose(read_outer)(1.0, 1.0)` **都不中止**，是两处干净的 `SemanticDiag::ArgumentsMismatch`
（`(float) => (float,)` 收了两个实参）。所以"两端口转置值被调用会中止"这句只在**造图侧已经落了两端口
承诺**的树上成立：那时被调值的记录类型是答案，不是函数，于是落进同一条判据。判据是通用的，
`transpose` 只是它今天最长的一条腿。

## 3. 两条路

| | 做法 | 代价 |
| --- | --- | --- |
| **A** | 复用 `SemanticDiag::ArgumentsRejected`：`{0}` 是写的被调名，`{1}` 是实参类型，`{2}` 写死一句真话（"这个调用所站的值类型是 `(float, float)`，它不是函数……"） | 头部一句完全准确；**但它的 help 一句不属于这一类**（"argument kinds match the operator's signature … tensor shapes or dtypes"），读者会读到一句与本例无关的建议。不改 `diagnostics/registry.tsv`、`src/core/error/**`，与本车道允许动的范围一致 |
| **B** | 在 `diagnostics/registry.tsv` 里加 `CalledValueNotAFunction`（紧接 `CalledFallibleValue` 的 0x000209 之后），跑 `--write` | 一句完全对得上的话；代价是 `registry.tsv` + 两个生成文件（`semantic.h`/`semantic.cpp`）+ `docs/diagnostics.md` + `docs/reference/camel-for-ai-diagnostics.md` + `examples.tsv` 共六处，还要过 `--no-producer-check` / `--name-coverage` / `--text-coverage` 三道地板（新码会把该 section 的文本覆盖比例拉低，测出来才知道）；`src/core/error/**` 不在本车道允许动的清单里 |

## 4. 我方默认

**A，本轮已落。** 理由：本车道的验收是"不许中止 + 判据不许放宽成什么都认"，A 两条都满足，而且
只动 `src/compile/gir/**`；B 的收益是一句话，代价是四到六个生成文件的 churn 和一条无法在本机
预演的覆盖率地板。**下一轮**若作者裁定要一句话，B 是机械动作：`registry.tsv` 加一行（空号，
`--write` 分配），把 `builder_operators.cpp#Builder::visitLinkNode` 里那一处 `.of(...)` 换名即可。

**并附一句**：`transpose` 那半条腿的落法（`2026-11-04-how-is-a-two-port-transposed-value-called.cn.md`
第 3 节 A）不依赖这道题——两端口承诺落下后，被调值是 `(float, float) => (float,)`，`visitLinkNode`
走的是与 `jvp(f)(2.0, 1.0)` 完全相同的 `CALL` + `WITH` 语句，本轮的改动不经过它。

## 5. 顺路一条：`split(jvp(g))` 被当元组拆，不是同一个位置

同一轮被问到：`let (fwd, lin) = split(jvp(g))` 报
`Type '(float) => (float, (float) => float pure) pure' does not support index access`。**不是**"取多一层"。

- 结果类型算在 `modules/transforms/split_operator.cpp#splitOperatorSignature`（`factory.setExitType(halves.pair)`），
  再经 `src/compile/gir/draft_graph_builder.cpp#DraftGraphBuilder::addOperNode` 取**一次**
  `op->funcType()->exitType()`——与 `transpose` 同一处、同样只取一次，没有第二次 fold。
- `halves.pair` 在 `modules/transforms/split.cpp#splitHalvesWith` 里就是 `halves.forward`（作者
  2026-11-02 05:10 裁定："`split` 的前半交出 `(y, lin)`"），所以 `split(X)` 的类型**本来就该是前半那个函数**，
  元组是**调用它**才拿到的（`test/cases/transforms/split/split_forward_half.cml` 是样板：
  `let fwd = split(jvp(g))` 然后 `let (y, lin) = fwd(3.0)`）。
- 所以那三条用例是在拆一个函数：**程序错，措辞够用**。改写法即可，`split*.cpp` 不该动。

## 6. 落点与读数

提交 `13e0b8994`（`lane-linkfn`，五个文件）；`git diff -w` 与 `git diff` 的 numstat 逐行相同
（`37/6` 一个 hunk），无重排、无空白噪音。

树 `13e0b8994`（clean，宿主 88zjwei，2026-11-04 06:2x，1 m load 15.4–16.7）+ 基线 `34b659107`
（`statetypes-base` 自量，1 m load 22.6）：

| 目标 | 我的树 | 基线 |
| --- | --- | --- |
| `test/plans/feat/compile` | 2245 / 2232 pass / **2 fail** / 11 known | 2243 / 2230 / **2 fail** / 11 known |
| `feat/compile/function_values` | 19 / 19 pass / 0 fail | 17 / 17 / 0 |
| `test/plans/feat/transforms`（目录） | 29 / 21 pass / 0 fail / 8 known | 29 / 21 / 0 / 8 |
| `feat/transforms/split` | 19 / 19 pass / 0 fail（19 个执行项） | 19 / 19 / 0 |

两处红在基线上就是那两条 `transpose_loop_escape`（`lane-loopescape` 的 `e16923e03` 修的，尚未合），
**红数 2 = 2**。

**拒绝读数**（改后）：`called_value_is_not_a_function.cml:17:14`，`SemanticDiag::ArgumentsRejected`
`0x0300001C`，exit 1，输出里没有 `Camel Invariant Violation`；`two_arguments_to_a_one_port_member.cml`
的两个调用 `28:17` 与 `29:34` 各一处 `SemanticDiag::ArgumentsMismatch` `0x03000010`，exit 1。

**图（`std::gir`，改后的二进制）**：`N0D [DATA "Function(graph=read_outer)" value_type="(x: float) => float pure"]`
在 `NORM` 上喂 `N0E [OPER "transforms:transpose" value_type="(float) => (float,)"]`，`N0E -> N10 WITH`、
`N0F (1.0) -> N10 NORM`、`N10 [CALL value_type="(float,)"]`。两端口承诺落下后，`N0E` 的 `value_type`
就是解析器答的 `(float, float) => (float,)`——节点类型只在
`src/compile/gir/draft_graph_builder.cpp#DraftGraphBuilder::addOperNode` 取一次 `exitType()`，
而调用语句与 `jvp` 那条完全相同：`N03 [OPER "transforms:jvp" value_type="(float, float) => (float, float) pure"]`
→ `N06 [CALL value_type="(float, float)"]`，`WITH` + `NORM 0|0` + `NORM 0|1`。**一个实参和两个实参走的是
同一句**，所以本轮的改动没有为两端口开任何第二条路。

**调查（只报不改）**：全树 `git grep "exitType()->exitType"` **零命中**；`visitLinkNode` 里还有两处与
本轮同类、未检查的 `as_ptr<FunctionType>`——`callDecorated`（`nodeTypeOf(decoratorInvoke)`）与重载挑选的
`signatureOf`（`it->second->funcType()->exitType()`）：两处的类型按构造都是函数，今天成立，但若哪天不
成立，代价仍是崩溃而不是诊断。