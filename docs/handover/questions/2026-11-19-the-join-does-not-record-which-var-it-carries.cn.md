# JOIN 不记「自己是哪个 `var` 的版本」，打印器无法重述携带的分支
2026-11-19 ｜ 来自 `lane-arm-carry-write` ｜ 对象：`Builder::buildBranch`（`src/compile/gir/builder_control.cpp`）的 S2 携带、`std::cml` 打印器（`src/passes/trans/cml/cml.cpp`）

## ① 卡在哪：图对，文本不可能对

「臂内的写被捕获、臂里写之后的语句消失」已经修好（`855882bc2`，`layRegionWriteEdges` 的
`readerTakesExit`）。修好之后 `arm_read.cml` 跑出注释里的 `int 3 4` / `int 0 3`，而
`print_and_recompile` 仍红——红的已经不是图，是**打印器没有办法把这张图重述回等价的源**。

`picked` 的图（修好后）：

```
var t = 3
if c then { let before = t; let cml1 = t + 1; t = cml1; report('int', before, cml1) }
else      { report('int', 0, t) }
return t
```

`return t` 里的 `t` 被携带折成了 **JOIN**（`8013a20f1` 的 S2：臂后读到的是 JOIN），体出口的 GATE
直接读那个 JOIN，于是打印器写出来的是：

```
return if c then { ... } else { report('int', 0, t) }
```

这一段重建时**臂后没有读**，携带不成立：JOIN 变成「并起两个臂各自的值」而不是「并起两个臂写下的值」，
canonical 文档少了一个 `GATE`、一个 `:var/get`（校验器原话，第一版）：

```
DIFFERENT: the two canonical documents are not equivalent
a structural class has 1 vertex/vertices on the left and 0 on the right
  left:  N:g3:n15: {"id": "n15", "inputs": [{"kind": "normal", "slot": 0, "source": "n2"},
         {"kind": "normal", "slot": 1, "source": "n14"}], "kind": "GATE", "region": 2, "type": 2}
  98 other class(es) differ as well: a class is a label and its neighbours, so one changed vertex
  re-colours the classes around it
```

（先说清一件事：`print_and_recompile` 在改动前就是红的——`91f708389` 上 `feat/trans` 109/108/1，
唯一的红就是它，因为那时臂里的 `report` 连节点都不是。这条红不是本笔引入的。）

**根因一句话**：图里 JOIN 只记了「我并起这两个臂」（`GCJoinBody{brch}`），**没记「我是 `t` 的版本」**。
打印器要写出 `if ... else ...` 当语句 + `return t`，就必须知道那个名字；从图上看不出来。

## ② 试过并回退的一版（不要把这一版当成解）

按「JOIN 某臂的 `with` 槽值 ＝ 该臂里某个 `:op/assn` 存下的值」认携带变量，并在「JOIN 唯一的 norm 读
者就是图出口」时，把它打印成语句 + 变量名。结果：

- `arm_read`：**`EQUAL: the two canonical documents are equivalent (52 vertices, 180 edges)`**，打印出的
  文本正好是源的样子（`if c then {...} else {...}` 然后 `return t`）——形状猜对了。
- 但同一版把别的用例的**语义改坏**了：`print_then_run_agrees_with_the_origin`

  ```
  case=linear/runtime/var_read_before_region rc=0 stdout=differs at line 3
    line 3 before: "0 5 1"   the other side: "5 5 1"
  case=linear/runtime/var_read_before_write rc=0 stdout=differs at line 4
    line 4 before: "arm 0 5 1"   the other side: "arm 5 5 1"
  case=linear/runtime/order_edges_regions rc=56 stdout=identical (the printed program exited 54)
  ```

  即打印出来的程序读到的是**旧版本**。收窄成「至少一个臂没写」（`arm_read` 正是这个形状）也没救回
  `var_read_before_*`。⇒ **全部回退**（`git checkout -- src/passes/trans/cml/cml.cpp`）：一个让一条绿、
  让另一条错值的修法不是修法。回退后 `feat/trans` 回到 109/108/1，`print_then_run_*` 两条都 PASS。

## ③ 正路（要作者点头，代价比这 50 行大一个量级）

把携带的事实**留在图里**而不是让打印器猜：

- `Builder::buildBranch` 在 `carriedVersions_[variable] = joinNode` 的同时，给 JOIN 记一个「我携带
  `variable` 这个单元」的标记（draft 侧一个字段即可，和 `:var/new` 的 cell 引用同一套 id）；
- `std::cml` 打印 JOIN 时读它：携带 ⇒ 写 `if/match` 语句 + 变量名；否则维持现状（值）。

要动 `builder_control.cpp` 的图元数据、`draft.h` 的形状、以及 `cml.cpp` 的 `branchExpression`；
`std::jgir` 的 canonical 文档要不要暴露这个标记也要一并决定（暴露就改 `cml_roundtrip.py` 的等价判据）。

## ④ 默认建议（写死，等人翻案）

**先不记，`print_and_recompile` 留着红。** 理由：

1. 它是**打印器**的缺口，不是「效应不静默消失」的缺口——语句现在真的会跑，`std::nvm` 的 stdout 与
   源注释一致，`std::gir` 里 CALL 在图上；
2. 这一笔的判据（最小复现图里有 CALL 且跑出 `t 3 4`、`arm_read` stdout 等于自己的注释、六条计划与
   `feat/modules` 不变）都不经过打印器；
3. 记进图会改打印器的输入模型，风险面（`var_read_before_*` 那类语义）刚刚被实测证明不便宜。

⇒ **请裁**：`print_and_recompile` 是否属于 0.2.45 的阻断清单。属于，就按 ③ 做（那就该在
`lane-arm-carry-write` 之外单开一条，因为要改打印器的输入模型）；不属于，本笔到此为止。