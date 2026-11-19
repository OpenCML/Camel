# 容器持有的元素：把它写成一条子句，并给它一条能说不的读数

**日期**：2026-11-20
**车道**：`lane-container-elements`（基线 `origin/develop = 740c8c9c6`）
**状态**：判完，**未改代码**、**未上主机**（两个并发位被 `ordedge` 与 `vjp` 占着，等位）
**上游**：[2026-11-19-an-in-place-argument-has-to-be-one-this-graph-made.cn.md](../questions/2026-11-19-an-in-place-argument-has-to-be-one-this-graph-made.cn.md)
§5 把这一半记成「缺陷本身」，并写死「默认甲」。本页做那页留下的两件事：给这半一条**可测的判据**，并回答「哪一条改动能让守卫不必再挡」。

## 1. 判据（一条能说不的性质）

**P**：设 U 是一个更新节点（`OperatorTable::inPlaceForm` 有、`plainForm` 无；`camel inspect --copies` 给它一行）。设 `given` 是 U 的数组操作数。若 `given` 是**从容器里读出来的**，则 U 的 verdict 必须是

```
copied          既不许 in-place，也不许 by-count
```

「从容器里读出来的」今天有四个形态，逐个对应一个节点：

| 形态 | 节点 | 容器是什么 |
| --- | --- | --- |
| 结构体 / 元组字段读 | `GCNodeKind::Accs` | 结构体、元组、闭包捕获的元组 |
| 引用数组的元素读 | `:op/idx_arr`，结果类型 code 是 `Array` | 外层数组 |
| 变体字段读 | `:enum/field`（`declareHandsOnOperandValue`，src/builtin/executors/builtin.cpp#declareHandsOnOperandValue） | 变体（`enum`/状态值） |
| 盒子读 | `state::get` | `state::Ref` 盒子 |

**它在哪里会说不**：把 `valueMadeHere` 从 `src/execute/exec_plan.cpp#planStorageReuse` 的那个条件里去掉，§2 的程序必须打印 §2 的「错值」两行。这两行不是推的，是两条用例头里记下的实测（`02939ffb7` 之前）。

## 2. 判据程序 `container_held.cml`

```cml
type Bag = { label: string, xs: int[] }

func main(): int sync {
    let a: Bag = { label: 'a', xs: [1, 2] }
    var b = a
    b.xs[0] = 9
    println(a.xs, b.xs)

    let rows = (0..2)->map<(i: int): int[] => [i]>
    let grown = rows[1]->append<2>
    println(rows, grown)
    return 0
}
```

**正确输出**（写死）：

```
[1, 2] [9, 2]
[[0], [1]] [1, 2]
```

**正确 verdict**（写死，`camel inspect --copies`）：按行读，不看列号——
名字列每出现一次 `set_index` 或 `append`，verdict 列必须是 `copied`；计数行必须是

```
COPIES main updates 2 in-place 0 by-count 0 declared 0 copied 2
```

（`map` 的元素函数是另一张图，打印 `updates 0`。）

**错值**（去掉 `valueMadeHere` 之后的读法，逐字取自两条用例的实测）：

```
[9, 2] [1, 2]          // two_names_for_a_struct：a 看见了 b 的写
[[0], [1, 2]] [1, 2]   // value_used：写进了 rows 里的内层数组
```

## 3. 今天两条用例：**守卫挡住了**，不是「本来就对」

`src/execute/exec_plan.cpp#nothingElseReadsIt` 单独对两者都答 **true**：

- 容器读的节点不在任何人的 `carried` 里被改写——不是 carrier 的节点 `carried[at] = {at}`（`src/execute/exec_plan.cpp#planValueUses`），于是 `carried.front() == value` 那一关过了；
- 更新是它唯一的读者，`reads == 1` 成立；
- 容器的持有**不在 `users` 里**：`users[rows]` 里只有那次元素读，`users[那层结构体]` 里也只有那次字段读。图里没有一条边说「容器仍然持有这个元素」。

所以放行的只有 `src/execute/exec_plan.cpp#valueMadeHere`（`default: return false` 那一支）。**结论：守卫是承重的，不是冗余的。** 用例头记的当天读数也印证：`two_names_for_a_struct` 打印「`set_index copied` 三次」。

## 4. 为什么 `by-count` 也不行（这一条最容易被当成「保守回落」而写错）

`by-count` 是「让数组自己的计数回答」，而对容器持有的元素，那个计数**看不见容器的持有**：

- `src/core/rtdata/array.cpp#writableInPlace` 答的是**这一个数组的 block 引用数**；同一个函数里 `isSlots()` 那支更直接无条件 `return true`，理由写在 `include/camel/core/rtdata/array.h#writableInPlace`：「第二个名字拿到自己的 `FixedArray` 副本」。
- 数「名字」的前提是**每一条通往第二个名字的路都跑过 `shareForSecondName`**（`include/camel/core/rtdata/array.h#shareForSecondName` 列的就是那几条：把一个值读出变量的读、往变量单元里的存）。**容器的一个槽不是这样的路**：它存的是内层数组的指针，不取 block 引用；结构体给第二个名字的是同一个结构体对象，叶子一个都不计。
- 于是 `writableInPlace()` 对一个容器仍然持有的内层数组答 true，原地形式（`src/builtin/operators/struct.cpp#__append_in_place_arr__`）就写进去了。`value_used` 的 `[[0], [1, 2]]` 就是这么来的。

**一句**：`value_used` 的错值不只否掉了 `in-place`，它同时否掉了 `by-count`。把容器形态接到 `ByArrayCount` 上会**照样错**，只是错得更晚。

## 5. 还有哪些形状属于「容器持有」而今天没被计入

按 §1 的四个形态落，另有组合：

1. **结构体字段读**（`Accs`）：`b.xs[0] = 9`；容器 `a` 与 `b` 是同一个结构体对象，`var b = a` 不为叶子计数。—— 用例已覆盖。
2. **嵌套结构体字段读**（`Accs` ×2）：`moved.box.xs[1] = 8`；容器是 `outer`/`moved` 那一层，持有链更长但同一条子句。—— 用例已覆盖。
3. **引用数组的元素读**（`:op/idx_arr`，结果 code = `Array`，所以 `src/execute/exec_plan.cpp#forwardsItsValue` 答 false）：`rows[1]->append<2>`；容器是 `rows`。—— 用例已覆盖。
4. **元素读上的写**：`let m = [[1, 2], [3, 4]]`，然后 `m[0][1] = 9`；`set_index` 的数组来自元素读，与 3 同一个 `given`——今天**没有用例**。
5. **变体字段读**（`:enum/field`）：`match v { case Holder(xs) => xs->append<1> }`；它是 carrier，但 `valueMadeHere` 仍答 false——今天**没有用例**。
6. **盒子读**（`state::get`）：`state::get(box)->append<1>`；盒子里那个数组与读出来的是同一个对象，而 `state::get` 不是 carrier（只有 `:enum/field` 与 `:op/idx_arr` 声明了），于是 `nothingElseReadsIt` 会放行——今天**没有用例**。
7. **数组套结构体**：`bs[1].xs[0] = 9`；3 与 1 的组合，`given` 是 `Accs(:op/idx_arr(...))`——今天**没有用例**。
8. **闭包捕获的容器**：`let f = (n: int[]) => n->append<1>` 里捕获在闭包元组里，比 `f` 的调用活得久；捕获是 `Fill`（`packsItsInputs`）打包的，读回来是 `Accs`——落在 1 上，同一条子句。

1-3 已有用例；4-8 今天只有代码位置，没有读数，**这是本页之后最该补的东西**（补的时候注意：新增 `.cml` 会动 `test/plans/feat/parse/golden/ast_hashes.txt` 的 2,569 行覆盖，必须同一次重录）。

## 6. 判定

**这两条：不需要改。`valueMadeHere` 是今天正确的判据**——它答 `copied`，而 §4 说明 `copied` 正是今天唯一的正确答案。让它放行所需的那个事实（「容器已经不再持有这个元素」）今天没有任何地方记录它：图里没有这条边，运行时计数也看不见它。所以「守卫挡住了」不是守卫的越权，是它替一份**不存在的计数**保守作答。

**能让守卫不必再挡的那一条改动在运行时，不在计划里**：让容器对它持有的元素记一次持有（引用数组的槽对元素数组取一次 block 引用；结构体为第二个名字计叶子），于是 `by-count` 对容器读成立，`valueMadeHere` 的容器那一半可以整条删掉。代价就是上游页 §5 的（乙）：动运行时引用计数与 `module/state` 的 `counted` 声明，范围远大于本车道。

**在那条改动之前能做的一条，且不改变任何判词**：把容器读从 `valueMadeHere` 里单列成 `readOutOfAContainer(graph, given)`（§1 的四个形态），条件写成 `valueMadeHere && !readOutOfAContainer && nothingElseReadsIt`。它让守卫只回答「来源」，容器这一半有自己的名字、自己的 doc 注释、自己的读数（§2）；判词必须**一字不差**——六计划 + §2 的两条读数。这一步才是「让守卫不再承担它本不该承担的职责」在今天能做到的全部。

## 7. 等主机

本线未构建、未跑计划。拿到位后：

```
pwsh -NoProfile -File D:\Projects\Camel-wt\container\internal\tools\camel-remote.ps1 -Path D:\Projects\Camel-wt\container -Plan feat/linear -Jobs 8
```

先跑六计划取基线（必须与 `origin/develop` 一字不差，本线只加一页 `.md`）；确认后把 §2 落成 `new_values.container_held.copies`，并给 `new_values.two_names_for_a_struct` / `new_values.value_used` 各补一条 `.copies` 读数——**那两条用例本身不许动**（不删、不编辑、不标 `known_fail`）。