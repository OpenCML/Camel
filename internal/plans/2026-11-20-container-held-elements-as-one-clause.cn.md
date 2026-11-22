# 容器持有的元素：把它写成一条子句，并给它一条能说不的读数

**日期**：2026-11-20
**车道**：`lane-container-elements`（基线 `origin/develop = 740c8c9c6`）
**状态**：判完并落地读数；**`src/` 一行未动**（判定是不需要改）
**上游**：[2026-11-19-an-in-place-argument-has-to-be-one-this-graph-made.cn.md](../reviews/2026-11-19-an-in-place-argument-has-to-be-one-this-graph-made.cn.md)
§5 把这一半记成「缺陷本身」。本页做那页留下的两件事：给这半一条**可测的判据**，并回答「哪一条改动能让守卫不必再挡」——现在每一句都有 §7 记的那几次运行做读数，不是推的。

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

**它在哪里会说不**：把 `valueMadeHere` 从 `src/execute/exec_plan.cpp#planStorageReuse` 的那个条件里去掉，§2 的程序必须打印 §2 记的「错值」两行（那是两条用例在 `02939ffb7` 之前的实测），两行 `--copies` 必须从 `copied` 变成 `in-place`。§7 的三条断言两个方向都测过：真读数通过，改成 `in-place`/`by-count` 就红。

## 2. 判据程序 `test/cases/consistency/new_values/container_held.cml`

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

**输出**（实测，2026-11-20，`740c8c9c6`）：

```
[1, 2] [9, 2]
[[0], [1]] [1, 2]
```

**verdict**（实测，逐字）：

```
COPIES __root__ updates 0 in-place 0 by-count 0 declared 0 copied 0
COPIES container_held.cml:26:9 set_index copied
COPIES container_held.cml:30:32 append copied
COPIES main updates 2 in-place 0 by-count 0 declared 0 copied 2
COPIES __g_48__ updates 0 in-place 0 by-count 0 declared 0 copied 0
```

**错值**（去掉 `valueMadeHere` 之后的读法，取自两条用例的实测）：

```
[9, 2] [1, 2]          // two_names_for_a_struct：a 看见了 b 的写
[[0], [1, 2]] [1, 2]   // value_used：写进了 rows 里的内层数组
```

## 3. 今天两条用例：**守卫挡住了**，不是「本来就对」

`src/execute/exec_plan.cpp#nothingElseReadsIt` 单独对两者都答 **true**：

- 容器读的节点不在任何人的 `carried` 里被改写——不是 carrier 的节点 `carried[at] = {at}`（`src/execute/exec_plan.cpp#planValueUses`），于是 `carried.front() == value` 那一关过了；
- 更新是它唯一的读者，`reads == 1` 成立；
- 容器的持有**不在 `users` 里**：`users[rows]` 里只有那次元素读，`users[那层结构体]` 里也只有那次字段读。图里没有一条边说「容器仍然持有这个元素」。

所以放行的只有 `src/execute/exec_plan.cpp#valueMadeHere`（`default: return false` 那一支）。**守卫是承重的，不是冗余的。** 两条用例今天的读数（实测）：

```
# two_names_for_a_struct.cml
COPIES two_names_for_a_struct.cml:29:9 set_index copied
COPIES two_names_for_a_struct.cml:38:13 set_index copied
COPIES two_names_for_a_struct.cml:44:17 set_index copied
COPIES main updates 3 in-place 0 by-count 0 declared 0 copied 3

# value_used.cml
COPIES value_used.cml:8:15 append copied
COPIES plus_two updates 1 in-place 0 by-count 0 declared 0 copied 1
COPIES value_used.cml:13:66 append copied
COPIES value_used.cml:13:83 append copied
COPIES main updates 2 in-place 0 by-count 0 declared 0 copied 2
```

## 4. 为什么 `by-count` 也不行（这一条最容易被当成「保守回落」而写错）

`by-count` 是「让数组自己的计数回答」，而对容器持有的元素，那个计数**看不见容器的持有**：

- `src/core/rtdata/array.cpp#writableInPlace` 答的是**这一个数组的 block 引用数**；同一个函数里 `isSlots()` 那支对 slot 数组无条件 `return true`，理由写在 `include/camel/core/rtdata/array.h#writableInPlace`：「第二个名字拿到自己的 `FixedArray` 副本」。
- 数「名字」的前提是**每一条通往第二个名字的路都跑过 `shareForSecondName`**（`include/camel/core/rtdata/array.h#shareForSecondName` 列的就是那几条：把一个值读出变量的读、往变量单元里的存）。**容器的一个槽不是这样的路**：它存的是内层数组的指针，不取 block 引用；结构体给第二个名字的是同一个结构体对象，叶子一个都不计。
- 于是 `writableInPlace()` 对一个容器仍然持有的内层数组答 true，原地形式（`src/builtin/operators/struct.cpp#__append_in_place_arr__`）就写进去了。`value_used` 的 `[[0], [1, 2]]` 就是这么来的。

**一句**：`value_used` 的错值不只否掉了 `in-place`，它同时否掉了 `by-count`。把容器形态接到 `ByArrayCount` 上会**照样错**，只是错得更晚。

## 5. 还有哪些形状属于「容器持有」

1. **结构体字段读**（`Accs`）：`b.xs[0] = 9`；容器 `a` 与 `b` 是同一个结构体对象，`var b = a` 不为叶子计数。—— §2 覆盖。
2. **嵌套结构体字段读**（`Accs` ×2）：`moved.box.xs[1] = 8`；持有链更长，同一条子句。—— `two_names_for_a_struct` 覆盖。
3. **引用数组的元素读**（`:op/idx_arr`，结果 code = `Array`，所以 `src/execute/exec_plan.cpp#forwardsItsValue` 答 false）：`rows[1]->append<2>`；容器是 `rows`。—— §2 与 `value_used` 覆盖。
4. **元素读上的写**：`var nested = [[5, 6], [7, 8]]`，`nested[0][1] = 9`。实测（2026-11-20）这条路径**有两条行**：外层那个槽的存是 `set_index_in_place in-place`（把改好的内层数组放回本图自己做的外层数组的一个槽，合法），内层的写是 `set_index copied`（容器读出来的那个内层数组必须复制）。**别把那条 `in-place` 读成这条子句失效**——它写的是外层数组的槽，不是元素本身。
5. **变体字段读**（`:enum/field`）：`match v { case Holder(xs) => xs->append<1> }`；它是 carrier，但 `valueMadeHere` 仍答 false——今天**没有用例**。
6. **盒子读**（`state::get`）：`state::get(box)->append<1>`；盒子里那个数组与读出来的是同一个对象，而 `state::get` 不是 carrier（只有 `:enum/field` 与 `:op/idx_arr` 声明了），于是 `nothingElseReadsIt` 会放行——今天**没有用例**。
7. **数组套结构体**：`bs[1].xs[0] = 9`；3 与 1 的组合，`given` 是 `Accs(:op/idx_arr(...))`——今天**没有用例**。
8. **闭包捕获的容器**：`let f = (n: int[]) => n->append<1>` 里捕获在闭包元组里，比 `f` 的调用活得久；捕获是 `Fill`（`packsItsInputs`）打包的，读回来是 `Accs`——落在 1 上，同一条子句。

5-8 只有代码位置，没有读数，**这是本页之后最该补的东西**（新增 `.cml` 要同一次重录 `ast_hashes.txt`，作法见 §7）。

## 6. 判定

**这两条：不需要改。`valueMadeHere` 是今天正确的判据**——它答 `copied`，而 §4 说明 `copied` 正是今天唯一的正确答案。让它放行所需的那个事实（「容器已经不再持有这个元素」）今天没有任何地方记录它：图里没有这条边，运行时计数也看不见它。所以「守卫挡住了」不是守卫的越权，是它替一份**不存在的计数**保守作答。

**能让守卫不必再挡的那一条改动在运行时，不在计划里**：让容器对它持有的元素记一次持有（引用数组的槽对元素数组取一次 block 引用；结构体为第二个名字计叶子），于是 `by-count` 对容器读成立，`valueMadeHere` 的容器那一半可以整条删掉。代价就是上游页 §5 的（乙）：动运行时引用计数与 `module/state` 的 `counted` 声明，范围远大于本车道。

**在那条改动之前能做的一条，且不改变任何判词**：把容器读从 `valueMadeHere` 里单列成 `readOutOfAContainer(graph, given)`（§1 的四个形态），条件写成 `valueMadeHere && !readOutOfAContainer && nothingElseReadsIt`。它让守卫只回答「来源」，容器这一半有自己的名字、自己的 doc 注释、自己的读数（§2）。这一步才是「让守卫不再承担它本不该承担的职责」在今天能做到的全部——**它要求判词一字不差**，而 §7 的三条断言就是那把尺子：任何一处判词移动，三条里必有一条红。

## 7. 落地物与读数

**落地物**（只在 `test/plans/**` 与 `test/cases/**`，`src/` 未动）：

- `test/cases/consistency/new_values/container_held.cml`（§2 的程序）；
- `new_values.container_held`：输出断言 `[1, 2] [9, 2]\n[[0], [1]] [1, 2]\n`；
- `new_values.container_held.copies`：`main updates 2 in-place 0 by-count 0 declared 0 copied 2`，两行均 `copied`；
- `new_values.two_names_for_a_struct.copies`：`main updates 3 in-place 0 by-count 0 declared 0 copied 3`，三行均 `copied`；
- `new_values.value_used.copies`：`main updates 2 … copied 2` 与 `plus_two updates 1 … copied 1`，三行 `append` 均 `copied`；
- `test/plans/feat/parse/golden/ast_hashes.txt`：`+ 53e82a2107e59d8a ok test/cases/consistency/new_values/container_held.cml`（重录 4450 行，**1 added / 0 removed**）。

**三条断言的反向验证**（本地，把真读数改成 `in-place`/`by-count` 再喂回去）：三条都红。判据能说不。

**六计划读数**（2026-11-20，`740c8c9c6`，`-Jobs 8`）：

| 运行 | 目标 | 读数 |
| --- | --- | --- |
| ① 只读树 | compile, linear, opt, parse, para, tools, **consistency** | `total: 6715, pass: 6693, fail: 2, skip: 2, known failures: 18`；两条红是 `runner.backend_matrix` 与 `new_values.sort_rebinding.graph`（上游页 §4 记的既有红） |
| ② 带本线改动 | consistency, linear | `total: 2161, pass: 2155, fail: 2, skip: 2, known failures: 2`；四条新条目全 PASS，红仍是那两条 |
| ③ AST 重录 | — | 4450 行，1 added / 0 removed |
| ④ 本线改动后 | parse | `total: 126, pass: 126, fail: 0`，`frontend.ast_snapshot` 与 `frontend.ast_snapshot_drop_guard` 均 PASS —— ③ 的 golden 成立 |

同一批运行里 `gpu-plans` 报 13 红（report-only，全部是已记录的 `tiles` 条目，与本次改动无关）。