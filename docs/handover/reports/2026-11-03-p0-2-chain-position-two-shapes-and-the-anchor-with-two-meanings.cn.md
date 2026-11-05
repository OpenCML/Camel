# P0-2 链位：块值那条路在文法上不存在，纯调用那条会断在读的定序上

两件事分开记：**(甲)** 图里"同步块的值"这个形状写不出来；**(乙)** 「纯调用的锚点不占链位」这个改法实测只红 2 条，但**它说明 `lastSyncedNode_` 一个变量承担了两个意思**。

树 `D:\Projects\Camel-wt\m4census`，基线 `origin/develop = 579d2c7ad`（全量 `total 8541 / pass 8507 / fail 14 / skip 10 / known 10`，红名单与 `1e6477a6f` 逐名相同）。命令 `camel-remote.ps1 -Path <该树> -Plan <plan> -Tier fast -Jobs 8`。

## 甲、`sync { … }` 不是表达式，所以「块值经 GATE」这条路不通

`sync` 在文法里是**语句/函数体的前缀**，不是能放在值位置上的表达式：

```
$ camel /tmp/p02_value.cml std::gir        # func main(): int sync { let a = sync { 1 + 2 } ... }
error[SyntaxDiag::UnexpectedToken]: Unexpected 'sync'; expected an expression.
  --> /tmp/p02_value.cml:2:13
 2 |     let a = sync { 1 + 2 }
   |             ^^^^
```
带效应也一样（`/tmp/p02_nested.cml:3:13` 同一条错）。而 `src/compile/gir/builder.cpp:1817` 那段"块值过 GATE"的代码本来就在，覆盖的是**函数体/块的值**这另一种情形。**⇒ 「`sync { 纯算术 }` 的块值要插 GATE」不是待修的缺陷，是写不出来的形状；不要再沿这条路走。**

## 乙、真的是纯节点在链上：纯调用的锚点

探针 `/tmp/p02_stmt.cml`（`func main(): int sync { println(1); let k = 1; let z = k + 1; println(z); return 0 }`）的图：

```
N01 -> N04 [label="SYNC", type="SYNC"]     // N01=:io/println, N04=:op/add_l
N05 -> N07 [label="SYNC", type="SYNC"]     // N05=:io/println, N07=GATE
```
`:op/add_l`（表里 pure、无效应、不出故障）占了一个链位。真用例 `test/cases/compile/return_type/nested_literals.cml` 同一形状：`N75 (:io/println) --SYNC--> N78 (:op/add_l, 48:19-48:20 的 k+1) --SYNC--> N84 (:str/format)`。

**改动**（`src/compile/gir/builder_operators.cpp:4387`，算子调用成为链位的那一处，全树唯一一处）：

```cpp
-    if (synced_) {
+    if (synced_ && nodeTakesAnOrder(targetNode)) {
```

`nodeTakesAnOrder` 是「这个节点是否不自由调度」的**现有问法**（`builder_helpers.h:526`，且能处理"节点正在构建、算子名还在 target 上"），不是新造的分类。diff `6 insertions, 2 deletions`（含注释），补丁在 `D:\Temp\p02c-pure-anchor.patch`。

**读数**：`feat/linear` 改前 `total 873 / pass 871 / fail 0 / known 2`，改后 `total 873 / pass 869 / fail 2 / known 2`。**两条现在绿的变红**：

| 名字 | 断言原文 / 缺的原文 |
|---|---|
| `captured_write_value.shape` | `Error: the call must not order the read N19 with a control edge` |
| `framed.loops.regions.lowered` | 输出不再包含 `... loop rounds: helper: the round contains OPER at pc=16`（节点少一个，pc 移位） |

## 丙、为什么红：`lastSyncedNode_` 一个变量两个意思

`captured_write_value.shape` 那条不是形状断言，是**语义断言**：一次读不许被一个 call 用控制边定序（`edges.cn.md` 5.5：读的次序来自它读的版本）。它红的原因在 `builder_expr.cpp:1745`：

```cpp
if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, value)) {
    writeControlLink(lastSyncedNode_, value);   // 读从这个锚点取次序
}
```

`lastSyncedNode_` 同时是 **(a)** 下一条语句连链的起点，**(b)** 读必须排在其后的"最后一条语句"。我让纯调用不前移它，(a) 对，(b) 错——读于是从**更早的那个 call** 拿到了一条控制边。

⇒ **正解不是"不推进锚点"，是把两个意思拆开**：保留 `lastSyncedNode_`（读定序用），另设一个"最后一个占链位的节点"，只给 `linkSyncStatement` 用。那个第二锚点同时就是"跳过纯调用之后链仍连通"的保证（`println(1)` 与 `println(z)` 之间没有数据边，靠它连）。落点三处：`builder_operators.cpp:4390`（连链）、`builder_expr.cpp:1745`（读定序）、`builder_control.cpp:4189`（返回值那条）。

**这一笔撤了，树上不留代码改动**（`git status --porcelain` 空）。按停手条件 (ii)，全量与 `feat/para` 没测：`feat/para` 新基线也未量（旧基线 `579d2c7ad` 之外的 `1e6477a6f` 上是 `63/63/0`）。