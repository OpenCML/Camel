# 生成器的状态按值在拉取之间传递：设计已定，代码未写

> 这一页是执行清单，不是裁定请求。三条设计结论都有证据；剩下的是一次 250 行左右的编译期改动，
> 分五步，每步都能单独编过。

## 为什么做这件事

`stream.generator{,nvm,fvm,jit,prl}` 五条在「删掉 `captureByValue` 的 gate5」之后退化为
`NoMatchingFunction: :op/assn_l`，根因是同一个闭包的类型有两处来源（`62` 页记了完整 trace）。
作者的裁定是两条：「`NoMatchingFunction` 是两份真相，不是语义问题」，以及「同一个闭包的类型必须
只有一个来源」。第 ④ 条：**机器自己的状态按值传递**。

## 三条结论（都有出处）

### 1. `StreamType::stepType()` 不能改

公开契约，`buildStreamLoop`（每个 `for x in s`）、`StreamType::assignableFrom`、stdlib 的 `tee`
都按 `() => (T, T..)?` 读它；`StreamType` 的键只有元素类型，程序写不出「状态类型」。
⇒ 改的是机器内部的 `__gen_step__`，不是 `stepType`。

### 2. 状态槽的零值：`A` 类进状态值，其余留格子

语料 300 个生成器捕获口（`list_sha=ac8288b4f2895df5a0dea5755a4b22c8d81e680524619f61cabc04b98c893f8b`）：

| 类 | 槽类型 | 口数 |
| --- | --- | ---: |
| A 有零值 | `var int`（绝大多数是 `__gen_pc__`/`__gen_halt__`，每个 `__gen_*` 图一对） | 274 |
| A | `var string` | 6 |
| A | `var { kind: string, message: string, location: string }` | 4 |
| A | `var (int, string)` | 3 |
| A | `var int[]` | 2 |
| B 函数 | `var () => int uses capture 0`（全在 `test/cases/compile/stream/generators.cml` 的 `read`） | 3 |
| C `Ref`/资源 | — | 0 |
| D 泛型参数 | — | 0 |
| E 需构造 | `var int!` | 4 |
| E | `var Option` | 4 |

`generators.cml` 摊开：14 个 `__gen_*` 图 → 28 个控制口 + 10 个程序局部（`i`×4、`x`×3、`read`×3）。
⇒ **`capture_ports_generator = 300` 里的大头是机器把自己的两个控制格子捕获了**，不是 300 个程序局部。
⇒ 落地后这个数预计 **300 → 约 11**（3 函数 + 4 `int!` + 4 `Option`）。目标仍是 0，差在这三类。

### 3. 「外层给状态槽一个值绑定」不需要 —— 撤回

`src/compile/gir/captures.cpp#Builder::decideCaptureByValue` 的答案取决于**哪个图在做捕获**：

```
compilerMadeClosure(graphName) == graphName.starts_with("__gen_")     // captures.cpp:333-340
if (compilerMadeClosure(graph->name()) || decoratedBlockGraph(graph)) return false;   // :245
```

今天真建答「格子」的唯一原因是：`__gen_step__` 为了触读把 `x` 捕获了一遍（`via=machine-closure
maker=true`），于是 `captureByValueOf_[x] = false`，之后 `__g_71__` 走 `via=cache` 拿到同一个
`false`。**第 ④ 条落地后步进不再触读 `x`（它成了步进自己的局部），那条 maker 答不再产生，缓存里也
就没有那个 `false`**；真建（`__g_71__`）与重建（`__typeof__`）于是走同一串门、同一个问题、同一个
名字。

**⇒ 外层的 `addCellOfType` 一行不动**（它只被类型查询解析到，没人读它）。GIR 也没有「名字直接绑一个
值」的形状：每个局部都是 `:var/new` + `:var/get`（`test/plans/feat/docs/fixture/images/10_var_order.dot`
的 `N07`/`N0A`）。

## 五步（每步单独编过）

1. **`zeroOf(Type *)`**：`src/compile/gct/builder.cpp` 里 `visitGeneratorBody` 旁边的**文件内静态**
   函数（匿名命名空间）。认 `int`/`float`/`bool`/`string`/tuple/匿名 struct/数组；tuple 与 struct
   **先递归判**，任一成员没有零值就整个槽返回 `nullptr`（不许造到一半抛）。函数类型、`int!`、
   `Option`、泛型参数返回 `nullptr`，函数头注释写明：*这些类型没有零值，槽继续留格子；将来要给它们
   一个「未写」形态，入口在这里。*
2. **状态类型与初值**：`S = TupleType::create({int, int, <A 类槽的 Type *…>})`；
   `S0` = 元组零值（`TupleDataFactory`），在 `outer` 里造。
3. **步进体解包**：`__gen_step__` 开头 `var <name> = <state 的 i 号成员>`（`tree_->makeAccs(i)`），
   顺序与 `S` 一致；`outer` 里同名格子保持原样。
4. **步进签名与驱动**：`FunctionType::create({S}, {}, stepType->exitType())`；
   `streamValue(state)` 的 lambda 体是 `__gen_step__(state)`；`outer` 返回 `streamValue(S0)`；
   每个 `yield` 处 `pairValue(elem, streamValue(packState()))`，`packState()` 读步进自己的局部
   （`writeCell(pc/halt)` 之后才打包）。
5. **剩下的是格子**：B/C/E 类照旧留格子；`__gen_touch__` 只对这些名字保留。

## 验收（照负责人给的清单）

- 四条线性形状（`count`/`quad`/`sign`/`reading`）的读数；
- **`62` 页第 1 点那两条 trace 的现在版本**——两边真的一致才写结论；「走同一串门答案必然一致」是预测，
  不是测量；
- `capture_ports_generator` 与 `capture_ports_closure` **分开报**，带 `list_sha`；
- `__gen_pc__`/`__gen_halt__` 进状态值会动 GIR 快照与 `parse/golden/ast_hashes.txt`：**单独一笔**。

## 不许写进提交正文的话

不许写「类型分叉已修好」。要写：**这五条的形状不再分叉，因为产生分歧的那一方（步进的触读捕获）
不再存在；缓存仍是「一格一个答案」，作者第四步（把三道门换成规则本身）之前，同一个名字被两个闭包
捕获时的分歧仍然可能发生。** 修一个形状不等于删掉那个机制。