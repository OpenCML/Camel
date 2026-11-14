# 库里的 `Ref<T>` 只能装整数：`Ref<T>` 该由核心提供一个内建吗？

**状态**：待裁。本批按文末的默认建议开工（`stdlib/state.cml` 已交付，`Ref<T>` 的能力以新写法回到
`test/cases/consistency/`）。

## 1. 裁定原文，与 `Cell<T>` 被取代的证据

`c8c110de2`「The explicit exit for long-lived mutable state is Ref<T>」的正文：

> The author's ruling: a handle type made by ref(v), read by get and written by set or update, passed
> explicitly; a Ref's content may not hold a Ref, so no cycle forms. Named Ref rather than Cell, since
> a cell is the storage of a var.

同一条提交改三个文件六行（`git show --stat c8c110de2`）：`docs/design-principles.{cn,en}.md` 各一行、
`docs/design/state/README.cn.md` 四行。那四行是**同一个位置**的替换：`cfc679795`（State draft: the real
costs, why others restrict the same way, and Cell as the exit）先把出口写成 `Cell<T>`，`01672d525`
（State design: items 1 and 2 decided）把它写进「已定」，`c8c110de2` 把第 4 节那段、第 8 节第 2 条与
总结句里的 `Cell<T>` 全换成 `Ref<T>`（`git show c8c110de2 -- docs/design/state/README.cn.md`）。
所以 `Cell<T>` 是被取代的旧名，不是并存的第二个类型，本页不复活它。

那段被替换的文字同时是本页要照做的约束，照录（只换名字）：`Ref<T>` 是句柄类型（原则 3.3，**每个值一个
资源**），在入口或调用者那里创建，**显式**作为参数或 `with` 传递，`get` 读、`set`/`update` 写，对同一个
单元的访问**按书写顺序**；离开调用的闭包可以捕获单元句柄（句柄是值），不能捕获 `var`；**单元的内容里不能
再有单元**（不成环，程序的值仍只靠计数回收）。

## 2. 库里写得出什么（本批已交付的形状）

`stdlib/state.cml`，四个函数一个类型，全部显式：

```cml
export type Ref<T> = { id: int }
export func ref::<T>(v: T): Ref<T> sync
export func get::<T>(r: Ref<T>): T sync
export func set::<T>(r: Ref<T>, v: T): T sync
export func update::<T>(r: Ref<T>, f: (x: T) => T): T sync
```

- **存储**是这个模块的一个 `var cells: int[]`，句柄是数组下标；`ref` 是 `cells->append<v>`，`get` 是
  `cells[r.id]`，`set` 是 `cells[r.id] = v`。`sync` 是必须的：不写在同步体里的写不会执行，编译器当时给的是
  「takes a var argument / is not in a sync block」与「The assignment has side effects but is never
  executed」，实测读到的是 `1 1 101 1 101`。
- **能力回来了**：`counter_closure.cml` 以 `state::ref` 重写，输出 `1 2 101 3 102`（与本树金标逐字相同）；
  新用例 `ref_one_cell_two_holders.cml`（一个句柄被闭包与调用方同时持有，输出 `1 2 2 11`）与
  `ref_takes_the_value.cml`（`ref` 取的是值，输出 `1 42`）锁住「跨调用存活」与「不偷渡格子」两件事。
  `test/plans/feat/consistency/values.plan.toml` 144/144 绿（四后端各跑一遍）。
- **做不到的四件事**（这就是本页的问题）：
  1. **只装整数**。存储是 `int[]`，`ref::<T>` 用 `match type T { case int => ... }` 分派，别的 `T` 在
     实例化处是 `TypeFunctionNotApplicable`。这是类型函数诚实的拒绝，不是漏写。
  2. **每个 `Ref` 不是自己的资源**。原则 3.3 的句柄类型（`uses r`）要求类型带 `#resource` 标签、每个值
     一个资源；库里的类型做不到——`cells` 是模块的 `var`，**一个 `var` 自动是一个资源**，所以全程序
     所有 `Ref` 之间都按书写顺序排序，两个无关的 `Ref` 也被排上了。想在签名里写 `uses r` 会当场
     `UsesPromiseViolated`，因为函数体写的是 `cells`。
  3. **不能释放**。没有 `drop`/`free`：格子只增不减，句柄的生命周期只到程序结束。裁定说「显式」包含生命
     周期，库里只做到「创建显式」。
  4. **泛型只在签名上**。`Ref<T>` 的 `T` 是一个不进入存储的类型参数，所以 `Ref<int>` 与 `Ref<float>` 是
     两个类型、却只有前者能造出来。

**本树的边界。** 这条裁定和它依赖的规则都还没进本基线：`git merge-base --is-ancestor c8c110de2 HEAD`
与 `… 4fae172f2 HEAD` 都为假，`docs/design-principles.cn.md` 里 `Cell`/`Ref` 零命中，
`src/compile/gir/builder_state.cpp` 也不在本树，原文与替换证据要用 `git show` 到 `origin/fsh-dev` 那一支
去读。所以「拿 `Ref<T>` 把 `var` 格子带出声明求值会被拒绝」这一条**在本树测不出来**。本树测到的是另一面，
也就是这条通道本来的性质：`ref(n)` 取的是值，`ref_takes_the_value.cml` 输出 `1 42`——`var` 后来的写看不见、
句柄后来的写也改不了 `var`，所以格子根本没被带走。规则落地后要用同一条用例复测。

## 3. 缺的那一项能力，以及「库里写不出来」的证据

缺的是**一项内建**：一个每个值各自是一个资源、内容可变、能被闭包按值带走、且对任意 `T` 成立的存储。
现有语言里没有第二个可变存储，`git grep` 的证据：

| 查询 | 结果 |
| --- | --- |
| `git grep -n "mutable" -- stdlib` | 两条：`stdlib/bytes.cml:5` 说「`Bytes` 是不可变的值」，`stdlib/state.cml:5` 是本批这一句 |
| `git grep -n "^var \|^export var " -- stdlib` | 父提交零命中，HEAD 只有 `stdlib/state.cml:24`：标准库里没有别的模块级 `var` |
| `git grep -n "^export resource" -- stdlib` | 四条：`process`、`rng`、`rendezvous_point`、`driver`——都是给外部资源命名，不是存储 |
| `git grep -n "export type Graph\|export type Txn" -- stdlib` | 不透明类型只能由模块提供（`stdlib/graph.cml:36-42`），库自己写不出带身份的值 |

其余三条路都试过或算过，都更差：记录（`type P = { ... }`）是不可变值（`docs/language/03_data_model.md:345`），
写字段就是造新值，两个闭包看不到对方；`var` 的格子属于声明它的那次求值（原则 3.5 规则二），把它带出去
正是刚合的那条规则拒绝的写法；`json`/`bytes` 编解码能把任意值塞进字符串，但那是序列化，`get` 变成可能
失败的转换，语义已经不是「一个格子」。

## 4. 三条路与代价

**A. 库里尽力接近（本批已做）。** 代价就是第 2 节那四条：只装整数、全程序共用一个资源、不释放、
`T` 只是签名上的参数。好处是零新语法、零核心改动，能力今天就能用，且**不碰那条规则**：`ref` 取的是值，
闭包捕获的是句柄（一个整数记录），没有任何 `var` 格子被带走。

**B. 最小增量：核心提供一个内建，不加语法。** `stdlib/state.cml` 里改成

```cml
export type Ref<T>                       // 不带 '='：由核心提供，身份是模块加名字（10.4）
export func ref::<T>(v: T): Ref<T>
export func get::<T>(r: Ref<T>): T uses r
export func set::<T>(r: Ref<T>, v: T): T uses r
export func update::<T>(r: Ref<T>, f: (x: T) => T): T uses r
```

与 `Graph`、`PyObject`、`Tensor` 完全同一个机制（无函数体的声明由 C++ 提供，`stdlib/graph.cml:36`），
因此**不是新语法**。它一次解决四件事：任意 `T`；每个值一个资源（`uses r`，与句柄类型一致，无关的
`Ref` 不再互相排序）；内容里不能再有 `Ref` 可以在类型处直接拒绝（`Ref<Ref<int>>` 无定义）；生命周期可以
显式（`drop` 一条声明）。代价：核心要有一个可变的堆对象（引用计数，`rt::Cell<T>` 已经是它的一半——
`docs/design/cpp-standalone/README.cn.md:265` 的 `var` 逃出函数时用的就是 `rt::Cell<T>`）；五个后端
（`std::nvm`/`fvm`/`jit`/`prl`/`std::cpp`）都要有这四个算子的实现；自动微分与 ONNX 导出要带位置拒绝
（与 `var` 的拒绝同一条）；`stdlib-reference --verify-ops`、`docs/cli*` 的算子表要跟着加。

**C. 让作者重新考虑。** 也就是承认「闭包按引用捕获 `var` 并离开那次求值」这条能力可以回来（撤
`4fae172f2`）。代价是那条规则封掉的洞重新打开：一个闭包能把格子带出声明它的求值，而它的类型什么都不说
（`() => int` 里看不出它写的是谁的格子），这正是该规则给出的理由（「a cell whose owner has returned is a
value nobody owns and nothing in the graph says so」）。不推荐，且与 `c8af40747` 的裁定冲突。

## 5. 默认建议（写死，按它开工，不等回复）

**取 A，并把 B 作为问题交给作者。** 具体：`stdlib/state.cml` 按第 2 节的形状留在库里（四个函数都 `sync`，
`match type` 只放 `int` 一个 case），三个用例与金标进 `consistency/values`，`stdlib/state.cml` 的模块注释
写死「只装整数、全程序一个资源、不释放」这三条边界，并在注释里指到本页；等 B 被裁定为「要」时，
把 `state.cml` 的四条声明换成无函数体的声明即可，**调用方的写法一行都不用改**——这就是把 A 做成这个形状
的理由：它同时是 B 的接口草稿。

**不要**在库里再往前一步（用 `json`/`bytes` 序列化当存储、或用 `PyObject` 当格子）：那会让 `get` 变成
可能失败的转换，并把「一个格子」偷换成「一份编码」，而这两件事都不该由一个还没裁定的替代品决定。