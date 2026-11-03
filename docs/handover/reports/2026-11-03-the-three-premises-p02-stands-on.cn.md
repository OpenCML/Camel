# P0-2 三个前提，今天逐条核（只读探针）

**问：作者在 T+72 那批里写「纯节点下链的第二步撤回（59 条红），**查清了**三个前提：`yield` 是链上语句、会故障的运算留在链上、没有前驱不造 `GATE`」。这三条今天到底是真还是假？**

出处：`docs/handover/batches-2026-10-31-72h.cn.md` §5 第 5 条（作者原文，逐字引在上面这句话里）。作者用的是「查清了」，不是「已实现」，所以这一页核的是**今天这棵树里的事实**，不是那次尝试的结果。

本轮**零构建**：探针用现成二进制跑（`~/camel-work/dedup/out/latest/bin/camel`，该树 = `origin/develop` `e5c4b844d` + 三笔文档/去重提交，与 GIR 无关），证据一律取 `std::jgir` 的 JSON，不靠推断。探针程序写在主机 `/tmp/p02_*.cml`，**没有落仓库**。

---

## 一条 `yield` 是不是链上语句 —— **真**

探针（`/tmp/p02_yield.cml`）：两行 `println` 之间各夹一条 `yield`。

```cml
func counted(): int.. sync {
    println("a")
    yield 1
    println("b")
    yield 2
}
```

`camel /tmp/p02_yield.cml std::jgir`，`__gen_step__`（g4）的 `sync` 边（`in`/`out` 都是 `sync` 边，不是 `control`）：

```
g4n19 OPER ':io/println'  in [g4n17]  out [g4n21]
g4n21 OPER ':op/assn_l'   in [g4n19]  out [g4n23]
g4n23 OPER ':op/assn_l'   in [g4n21]  out [g4n28]
g4n28 GATE                in [g4n23]  out []
```

两条 `:op/assn_l` 就是那条 `yield` 的两次写（元素一次、`__gen_pc__` 一次）——判断依据是位置：两行 `println` 之间只有 `yield` 一条语句，而这一段链上除它们之外没有别的语句节点。也就是说 **`yield` 不是一个游离节点，它占链位：它的写排在上一句之后、下一句之前，次序由 `sync` 边给出**。第二个 arm 是同形状（`g4n35 :io/println → g4n37 :op/assn_l → g4n39 :op/assn_l → g4n44 GATE`）。

循环里的 `yield`（`/tmp/p02_lp.cml`：`loop i = 0 while i < 3 do sync { yield i; i + 1 }`）同样落在这条链上：

```
g4n19 BRCH                in []       out [g4n21, g4n26]
g4n21 OPER ':op/assn_l'   in [g4n19]  out [g4n23]
g4n23 OPER ':op/assn_l'   in [g4n21]  out [g4n24]
g4n24 GATE                in [g4n23]  out []
```

旁证（不是这一轮的读数，是同一天别人量过的）：把纯节点从链上拿掉，`feat/compile.stream` 从 0 → 34 全红，`generator_sync_order*`、`generator_early_stop*` 在列——如果 `yield` 不占链位，拿掉链位不会动它们。

## 会故障的运算是不是留在链上 —— **真**

探针一（`test/cases/opt/camel_passes/fold_fault.cml`，`let a = 1 / 0` 与一个可折叠的 `x * y` 并存）：

```
g1n2 OPER ':op/div_l'    in []       out ['g1n5']
g1n5 OPER ':op/mul_l'    in ['g1n2'] out []
g1n6 OPER ':io/println'  in []       out ['g1n7']
g1n7 GATE                in ['g1n6'] out []
```

那句除零**没有被折叠/摘掉**：`div_l` 在图里，而且带着 `sync` 出边指向下一条语句。探针二（`test/cases/linear/operators/mod_assign_by_zero.cml`，`x %= 0` 那一族）：

```
g1 g1n5  OPER ':op/mod_l'  sync-in []
g1 g1n7  OPER ':op/assn_l' sync-in []
g1 g1n10 OPER ':op/mod_l'  sync-in ['g1n8']
g1 g1n12 OPER ':op/assn_l' sync-in []
```

两处 `mod_l` 都在图上，且各自带着 `sync` 入边/出边——**会故障的运算当语句留在链上，没有被当成可消除的纯计算**。这与 `camel_passes.fold_fault.{subject,property}` 那条「the division no longer orders the print」的读法一致：它的先后也是链位给的。

## 没有前驱的时候会不会造 `GATE` —— **真**（今天不造）

建图器（`src/compile/gir/`）里 `addGateNode` 一共四处，每处都要求一个前驱，没有一处是无条件造门：

- `src/compile/gir/builder_control.cpp#Builder::visitExitNode`（链尾，`4257`）：`if (!syncInputs.empty() && (!nodeIsKind(resNode, Gate) || !syncInputsOf(resNode).empty()))` —— **没有 sync 前驱就不进这个分支，门不造**。
- `src/compile/gir/builder_control.cpp#Builder::coerceArmExit`（分支 arm 的出口，`1349`）：`if (normInputsOf(coerced).empty() && !ctrlInputsOf(output).empty())` —— 要有 control 输入。
- `src/compile/gir/builder.cpp#Builder::…`（块的值交给链尾，`1973`）：门读的是 `res`，而 `res` 与 `lastSyncedNode_` 之间那条 sync 边就是它的前驱。
- `src/compile/gir/builder_operators.cpp#…`（一次写没有读者时把值交出去，`4509`）：`if (assignment && valueWanted && normInputs.size() == 2)`，门等的是那次写本身。

（`modules/autodiff/pullback.cpp`、`modules/transforms/jvp.cpp`、`modules/transforms/rule_expand.cpp` 里另有各自的 `addGateNode`，那是变换自己造门，不是建图器的规则，本页不据此下结论。）

图上对得上：`mod_assign_by_zero.cml` 的**第一条语句**就是这条形状——`g1n5 :op/mod_l sync-in []`，它前面**没有** `GATE`；而链尾的 `println`（有 sync 前驱）后面有一个 `GATE`（`g1n7 in ['g1n6']`）。所以「没有前驱不造 `GATE`」今天是真的。

**这条要说清的边界**：59 条红里那条封存拒绝（`Runtime graph 'jvp_of_g' has GATE node 8 without a wait: no sync predecessor and no control input`，`src/runtime/graph.cpp`）是被**撤掉的那一笔**造出来的形状，不是今天建图器的规则——今天建图器四处都没有「无条件造门」的路。所以第三步若要重开，这条前提仍然成立，但它不解释那条红；解释那条红的是「把纯调用语句的链位换成读它的 `GATE`」这个改法本身。

---

## 复现

```bash
export LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib
cd ~/camel-work/dedup
./out/latest/bin/camel /tmp/p02_yield.cml std::jgir   # 前提一
./out/latest/bin/camel /tmp/p02_lp.cml    std::jgir   # 前提一，循环里
./out/latest/bin/camel test/cases/opt/camel_passes/fold_fault.cml std::jgir        # 前提二
./out/latest/bin/camel test/cases/linear/operators/mod_assign_by_zero.cml std::jgir # 前提二
# 前提三：同一份 JSON 里的 sync 入边 + 上面四处 addGateNode 的条件
```

## 结论

| 前提 | 结论 |
| --- | --- |
| `yield` 是链上语句 | **真** |
| 会故障的运算留在链上 | **真** |
| 没有前驱不造 `GATE` | **真**（建图器四处 `addGateNode` 都以 sync 或 control 输入为前提） |

三条都真，所以第二步当初的失败**不是**因为前提不成立；那一笔要重开，得先解释第 3 节末尾那句边界——即「链位换门」这个改法本身为什么会造出没有前驱的门。