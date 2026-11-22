# develop 上有 21 条红，而带它们进来的那笔提交自己写着「先别合」

## 事实

`3773eed5c`（`var: delete the gate that kept a cell for a name handed to a call`）删掉
`src/compile/gir/captures.cpp#Builder::decideCaptureByValue` 里那道「交给调用点的名字保留格子」的门
（gate5），并把 `handedOver_` 与 `programMayWriteThroughCall` 一并删掉。

这笔的正文末尾写着：

> DO NOT MERGE THIS BRANCH YET. Deleting the gate turns 21 tests red, and they are not stale
> expectations -- the gate was standing in front of three real defects: ...
> They are recorded, not papered over: no expectation below is changed to make one of them pass, and
> no test is marked. **This branch waits until the three families are fixed.**

而 `git merge-base --is-ancestor 3773eed5c origin/develop` 是 **YES**。三族一条都没修好，
它就进了 `develop`。

## 三族

| 族 | 条数 | 性质 |
|---|---|---|
| `dead_write.*` | 9 | **写到被捕获的名字上，既不产生节点、也不产生诊断**：程序编过、跑完、把值打出来、一句话不说 |
| `nested_function_value.*` + `poison:compile.function_values:nested_function_value.*` | 7 | 编译期 SIGABRT，踩 `nodeTypeOf` 的 `node != nullptr` 保证（"Draft node is null"） |
| `stream.generator.*` | 5 | 生成器机器自己的状态写退化到 `NoMatchingFunction: :op/assn_l (var int, int)` |

## 为什么这一页必须存在

**这 21 条红和一次真正的回归长得一模一样。** 没有任何计划项把它们标成 `known`/`xfail`
（这是那笔提交的承诺：`no test is marked`），所以：

- 一个车道在 `develop` 上跑 `test/cases/linear` 看到 `dead_write.*` 九条红，无法分辨
  「这是亲笔记录的旧债」与「我刚才那笔改动弄坏了它」；
- 一个 ratchet（`test/tools/e9diff/baseline.json`、`ast_hashes.txt`、`var_new` 这类基线）
  若在带这 21 条红的树上录零点，**这 21 条就变成了「正常」**，而它们此后涨落都不再报警。

**「recorded」在这里的意思只是**那句提交正文里有一段文字。**提交正文不是索引，没人会去读 4000 笔里的这一笔。**

## 已经在动的一支

`stream.generator.*`（5）这一族有在做的活：`lane-typesrc` 的 ④（一个闭包只有一个类型来源）
与挂死修复（`ae9c32576`，循环 state 由「答案」改成 `(答案, 答没答)`）。**但那 5 条今天是红的，
而 ④ 帮不上**——状态元组装不下任何程序局部（`slots=[]`，格子是 `var T`，没有零值）。
它们要等作者已批的那条：槽写成 `T?`、拆开用带检查的算子。

## 要裁定的一件事（只有一个）

**剩下两族谁先，以及 `3773eed5c` 留不留。**

## 我的默认（若无答复，按此开工）

1. **`3773eed5c` 留。** 它删的那道门，作者在 2026-11-16 已经用一句「三种都不是：这个用例测的是已撤回的
   按引用语义，新语义下打印 0 是对的」撤掉了它等的那个形状。**回滚它等于把作者撤掉的规则装回去**，
   而回滚不会让那两族消失——它们本来就被那道门挡着。

2. **先修 `dead_write.*`（9 条），再修 `nested_function_value.*`（7 条），最后等 ② 那族的槽裁定落地。**
   理由不是条数，是那笔提交自己写下的那句：

   > a write to a captured name produces no node and no diagnostic at all: the program compiles, runs,
   > prints its value and says nothing. **A crash is loud; this is not.**

   **一个 SIGABRT 会自己找上门，一个静默的错误值不会。** 而在静默与崩溃之间，先修静默的那个。

3. **两条线各一条判据，不许共用**：
   - `dead_write.*`：那 9 条各自从红转绿，**且**新增一条用例证明「写到被捕获的名字上」有节点或有诊断
     （二者之一，按修法定），**不许只让 9 条变绿**——**9 条变绿可以是把诊断关掉。**
   - `nested_function_value.*`：那 7 条转绿，**且** `nodeTypeOf` 的 `node != nullptr` 保证在 SIGABRT
     那一条路径上仍然成立（即修的是那个 null 的来源，不是给它加一个判空）。

4. **这一页在 21 条全部转绿之前不删。** 它删掉的那天，应有一笔提交说「三族已修完，`3773eed5c` 的
   DO NOT MERGE 条件已满足」。

## 这一页不改变的事

本页不请求回滚、不请求跳过任何闸门、不改任何期望值。它只把一句写在提交正文里的债，搬到会被读到的位置。
## 决策者答复（2026-11-16）

**同意默认：`3773eed5c` 留，先修 `dead_write.*`，再修 `nested_function_value.*`，最后生成器那族。** 删那道门是对的（它护的是已撤回的按引用语义），先修静默错值、再修崩溃的顺序也对。

但合并本身是流程错误：一笔正文写着"DO NOT MERGE"的提交进了 develop。合并前读一遍被合提交的说明，写着不合的就不合；要合就先在说明里改掉那句话并写明理由。这条写进合并检查表。21 条红在修好之前逐条列在基线里，注明"由 `3773eed5c` 带入、待修"，不算进"新红 = 0"的口径之外。
