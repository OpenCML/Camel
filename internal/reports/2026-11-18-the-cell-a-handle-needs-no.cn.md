# 装句柄的格子：让它消失的那一半，和消不掉的那一半

- 日期：2026-11-18
- 车道：`lane-refcell`；基线 `origin/develop` = `eedfb3da0`
- 裁定：作者 `9d29939c4`（merge `develop` 进 `fsh-dev` 的提交说明最后一句，逐字）
  **「answer: a cell holding a Ref is a holder, and the cell should go」**
- 前史：`internal/questions/2026-11-18-a-cell-holding-a-ref-is-neither-counted-nor-checked.cn.md`
  （问句与三条路；第 4 节的默认建议「先只改判断」已随裁定作废——裁定给的答案比那页的默认更强）

## 1. 结论

裁定里那两半是两件事，落地也是两件事：

| 界面上有多少格子装句柄 | 形状 | 这一笔之后 |
|---|---|---|
| `var` 装 `state::Ref`，被闭包读 | `var box = state::ref(0); let read = () => state::get(box)` | **格子没了**：名字是值，闭包捕获句柄本身 |
| 生成器体里的局部 `let`/`var` 装 `state::Ref` | `let box = state::ref(0) … yield state::get(box)` | **格子还在**：这一格要走生成器的状态槽，而 `Ref` 没有「未写」的形态（第 3 节） |

前一半落地了，读数证明它落在图上（第 2 节）；后一半**没有落地**，钉在 `zeroValueOf` 那道门上（第 3 节），
这一笔没有为了让后一半变绿去动语义。

## 2. 前一半：`var` 装句柄是值，格子不再存在

### 2.1 改在哪

`var` 是不是格子一直由一次**建图之前的扫描**定（`Builder::scanBodyForSsa`，阶段 S1/S2/S3 的那张表）：
一次使用跨过一个「无处安放值」的区域（闭包体、装饰块、静态循环体、循环条件）就把声明判成格子。
`var box = state::ref(0)` 被闭包读正是这种形状 ⇒ 声明是格子 ⇒ 闭包共享这一格 ⇒ 盒子的持有者没人算。

`src/compile/gir/builder_expr.cpp` 的扫描现在给**两个**答案（`Builder::SsaAnswer`）：

- `asValue`：原来的答案，一格不动；
- `asHandle`：只有「跨区域的**读**」不算把声明判成格子。「跨区域的**写**」照样判成格
  ——写要有落点，值不是落点。

声明处（`visitNExprNode`，`builder_expr.cpp:505` 一带）按**初值是不是计数句柄**（`RefType::is(type)`）挑答案：

```
asHandle 的答案 ⇒ 名字不只是值，而且不该有格子     // var box = state::ref(0) 被闭包读
其他          ⇒ asValue 的答案（今天的行为）
```

配套的一处是 3.5 (198) 那条规则：**从闭包建立到它最后一次被用，名字不得被赋值**。原来的检查按**格子**
记账（`capturedVarOf_` 以格子节点为键），名字变成值以后就没有格子可挂。所以：

- `captures.cpp`：捕获一个「现在是值」的 `var` 名字时，照样记下这次捕获（`namedVar`，键是当时的**值节点**）；
- `builder_operators.cpp` 的 `ssaAssignmentOf`：写一个值名字之前问一次
  `requireCapturedNameWritable(<现在那个值节点>, …)`。

⇒ 两条诊断都还在（改完后在主机上直接跑的两个最小例）：

```
var box = state::ref(0); let read = () => state::get(box); box = state::ref(5)
  ⇒ error[SemanticDiag::CapturedVarWrittenAfterTheClosure]: 'box' is assigned after the closure 'read' was made from it
var box = state::ref(0); let bump = () => { box = state::ref(9) }
  ⇒ error[SemanticDiag::ClosureAssignsCapturedName]: 'box' is assigned inside a closure
```

第二条能立住，正因为扫描的 `asHandle` 对**写**说「不」：写跨闭包仍然留格子，于是原来那条以格子为键的检查
照旧生效。**两处判断（读跨闭包 / 写跨闭包）现在各说各的、而且都说得对。**

### 2.2 「格子不再存在」的证据不是 stdout，是图

`internal/archive/probe-refcell/cell_holds_ref.cml` 里那四个形态（`plain_let`、`var_same_region`、
`cell_read_by_closure`、`cell_written_and_read`），`std::jgir` 里 `:var/new` 出现的次数是 **0**
（改之前 `cell_read_by_closure` 有一个 `:var/new`，类型 `var Ref<int>`，`name: "box"`）。
闭包的捕获口仍然是 `Ref<int>`，也就是说闭包拿到的就是那个句柄。

改后同一份探针的 `std::jgir`，**逐图**列出所有 `:var/*` 节点：

```
cell_read_by_closure     （无）
cell_written_and_read    （无）
gen                      :var/new/empty  box        var Ref<int>     ← 第 3 节那一格
__gen_step__             :var/new       __gen_pc__  var int
```

运行读数（同一份探针，`--gc-check foreign`；跑法 `camel internal/archive/probe-refcell/cell_holds_ref.cml
--gc-check foreign`）：

```
改前  plain_let 5 / var_same_region 5 / cell_read_by_closure ⇒ InvalidArgument（早释放）
改后  plain_let 5 / var_same_region 5 / cell_read_by_closure 5 / cell_written_and_read 9
      第五个形态（生成器）仍然早释放 —— 见第 3 节
```

## 3. 后一半：生成器体里的局部，这一格消不掉

```
func gen(): int.. sync {
    let box = state::ref(0)      // :var/new/empty，类型 `var Ref<int>`
    state::set(box, 5)           // ⇒ InvalidArgument: the value is not a reference
    yield state::get(box)
}
```

改后仍然红，红因仍然是**早释放**（`FOREIGN_RESOURCES created=1 counted=1 released=1 live=0`：
盒子在建它的那一次拉取之后就被交还了，后面几次拉取拿到的是已释放的句柄）。

为什么消不掉，一句话：**一个 pull 里造出的盒子要活到后面几个 pull，所以它必须进生成器的状态槽；
状态槽要先有一个「未写」的值，而 `Ref` 没有这种值。**

读数是写死的（`src/compile/gct/builder.cpp:4090-4102`）：

```
`nullptr` 表示这个类型没有这种值，槽就继续用格子。那些类型是函数类型、`Ref` 和 `#resource`
（它们的值是一个调用造出来的盒子）、`T!` 与枚举、类型变量。
如果其中某一个将来要有「未写」形态，这就是那道门：一处决定，状态元组就不再需要格子。
```

生成器的局部今天只有两条路：类型有零值（`zeroValueOf` 非空）⇒ 进状态槽（`(pc, …slots)`）；
没有 ⇒ 留格子（`:var/new/empty`）。`Ref` 落在后者。

⇒ **给 `Ref` 定一个「未写」的形态是语言决定，不是这一笔能自己做的**：那个值会是一个「不是盒子的句柄」，
它要能被 `state::set` 前的任何一条路径安全地放着、要么由作者说它可以存在，要么说生成器体的句柄局部
另有落点。这一笔按任务书的第 ③ 条停在这里，没有为了让例子变绿去改语义。

同一页第 1 节还有一个可查的旁证：语料里**没有**这种形状的程序。`test/cases/` 下 76 处 `state::ref` 全是
`let`，其中唯一带 `yield` 的用例 `test/cases/compile/stream/generators.cml` 的盒子是**形参**
（`reading(box: state::Ref<int>)`），它的格子是形参格子，与局部那一格不是一件事。所以：

- `var_new` **不变**（下面读数里写着人口），因为人口里没有「`var` 装句柄」的程序；
- 变绿的是探针里的两个形态，不是任何语料用例。

## 4. 读数

一次 gate 跑完六个计划 + `linear`（`-Jobs 16`；同一份树，构建 `BUILD_EXIT=0`）：

```
[SUMMARY] total: 5319, pass: 5284, fail: 14, skip: 2, known failures: 19

feat/compile   2627 / 2605 pass /  5 fail / 17 known   红名 = refwrite.shapes{nvm,fvm,jit,prl,default}
feat/opt       1538 / 1538 pass /  0 fail /  0 known   （e9 在里面：212 / 212 / 0）
feat/para       104 /  104 pass /  0 fail
feat/parse      121 /  121 pass /  0 fail
feat/tools       70 /   70 pass /  0 fail
feat/linear     859 /  846 pass /  9 fail /  2 known / 2 skip
                红名 = element_assigned.in_place_form、element_assigned_append.in_place_form、
                       path_assigned.allocs{nvm,fvm,jit,prl}、var_tensor_local.{run,macro}、
                       inplace_write_then_read
```

这 14 名与基线逐条相同（`refwrite.shapes` 那 5 条是 `lane-refwrite` 的，`linear` 那 9 条是别的车道的），
**新增 0，变绿的 0**。`[TREE] clean`：跑完的树与跑之前的 7 条未提交路径一致。

`capture-census`（同人口，逐字相同，标的是**没动**）：

```
files=3891 listed=3891 counted=2728
var_new=213 var_new_empty=43 cell_typed_nodes=1188
capture_ports=177 capture_ports_generator=111 capture_ports_closure=66 capture_ports_value=2224 capture_ports_total=2401
list_sha=b7f5568773e8c150806f0feff99199655442b4cb8390728c7d28f4d34723aa09
outcomes: ok=2728 refused=1163 crashed=0 timeout=0 unreadable=0
```

**`var_new` 没有下降，而且不该下降**：它数的是 2728 个程序（3891 个文件）里的 `:var/new` 节点，
而那个人口里没有一个是「`var` 装 `state::Ref`」——76 处 `state::ref` 全是 `let`。
所以读数不动**不是没改**：改到的那两个形态只在探针里，语料里没有它们。这也是这一笔没有语料用例可钉的原因
（探针 `.cml` 留在 `internal/archive/probe-refcell/`，不进 AST 语料）。

`gpu-plans` 那 15 条红是**记录在案**的（`red_more=0`，15 个名字与记录逐条相同），与这一笔无关。

## 5. 没做的事

- 没有改 `valueIsCounted`（问句页第 4 节 ① 的那条路）——裁定之后那条路作废：格子不再存在的地方不需要判，
  仍然存在的地方（生成器局部）该由状态槽解决，而不是靠一处判断把它算成持有者。
- 没有给「仍然存在的格子」加运行时计数（问句页的路 A）。它只对生成器局部有意义，而那一格的去向是状态槽；
  现在给格子定义「死在哪一点」，等于把一个即将消失的形状写进计数表。
- 没有动 `zeroValueOf`、`visitGeneratorBody` 的状态元组、`docs/design-principles.cn.md` 5.4。
- 语料、断言、`known_fail`、`baseline.json` 都没有动。探针 `.cml` 放在 `internal/archive/probe-refcell/`
  （不进 AST 语料），跑法是 `camel internal/archive/probe-refcell/cell_holds_ref.cml --gc-check foreign`；
  两个临时计划文件（`test/plans/probe/refcell_*.plan.toml`）量完就删了，不进 `develop` 的计划树
  ——第五个形态在它们里面本来就是红的，留着只会让下一个人以为那是这一笔留下的红。