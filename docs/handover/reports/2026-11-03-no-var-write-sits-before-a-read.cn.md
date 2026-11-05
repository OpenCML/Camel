# `var` 的写会不会排在一次读之前：造不出反例

**问：P0-2 完成判据的第二条——「`var` 的写排在上次写之后的所有读之后」——今天成立吗？能不能造出一个写排在一次读之前的程序？**

出处：作者 T+72 批次 `docs/handover/batches-2026-10-31-72h.cn.md` §5 第 5 条，完成条件之一逐字为「`var` 的写排在上次写之后的所有读之后」。

## 1. 先写事实：这条约束今天已经写着，而且写着的地方只有一处

`src/compile/gir/builder_expr.cpp#Builder::recordWrite` 的注释就是这条规则本身，并注明作者 2026-11-03 的裁定与 `docs/design/effects/edges.cn.md` §5.6：

> for one cell, a write is ordered after every read since the write before it. A chain place is not the carrier of that order … so the order is written here, once, by the write.

写的时候把 `pendingUsesOf(cell)` 里攒下的每次读取出来，各连一条 `order` 边（读的价值已经沿数据路走到写上的则不再重复连，`x = x + 1` 是这种）。区域（JOIN / LOOP）上的读改连 `control`，因为区域是一整块排布的。所以这一轮**不是重新实现**，是核它有没有漏形状。

## 2. 六个形状，逐个看边（`std::jgir` 的 `order`/`sync`/`control` 边，原样抄）

探针在主机 `/tmp/vo2.cml`、`/tmp/vo3.cml`、`/tmp/vo4.cml`、`/tmp/vo5.cml`，**没有落仓库**；用现成二进制跑，零构建。

**(a) 直白形**（`var v = 1; println(v); v = 2`）：读与写之间的边就在写身上——

```
order g1n4 ':var/get'      -> g1n6 ':op/assn_l'
sync  g1n3 ':io/println'   -> g1n6 ':op/assn_l'
```

**(b) 读在一个分支臂里、写在该分支之后**（`if flag then sync { println(v) }` 之后 `v = 2`）：写等的是 JOIN，而臂里的读经由臂的语句进 JOIN——

```
control g2n4 'JOIN' -> g2n11 ':op/assn_l'
sync    g2n4 'JOIN' -> g2n11 ':op/assn_l'
sync    g2n6 ':io/println' -> g2n8 'GATE'      # 臂里的读→语句→GATE
context g2n8 'GATE'        -> g2n4 'JOIN'
```

**(c) 写在臂里、读在分支之后**：读等 JOIN（读拿的是写之后的版本），方向相反的一条也对——

```
control g2n4 'JOIN' -> g3n12 ':var/get'
```

**(d) 读在循环体里、写在该循环之后**：写等 LOOP 整块——

```
control g1n13 'LOOP while' -> g1n16 ':op/assn_l'
sync    g1n13 'LOOP while' -> g1n16 ':op/assn_l'
```

**(e) 读经由一个读了捕获变量的调用**（`show(v)` 之后 `v = 2`）：`order` 边直接来自那次读——

```
control g2n3 'show'     -> g2n6 ':op/assn_l'
order   g2n4 ':var/get' -> g2n6 ':op/assn_l'
```

**(f) 读在分支条件里**（`if v > 0`）：条件是 `:var/peek`，它**也被记成读**——

```
order g1n5 ':var/peek'  -> g1n13 ':op/assn_l'
control g1n6 'JOIN'     -> g1n13 ':op/assn_l'
```

**(g) 读在 `match` 臂里**：写经由臂的值与语句排在读之后（`g1n7 :var/get → … → g1n9 :io/println → sync → g1n11 :op/assn_l`）。

## 3. 结论：**造不出来**

**我造不出能证明它的东西。** 上面七个形状（直白、分支臂里读、臂里写、循环体里读、读经捕获调用、条件里的 `peek`、`match` 臂）都没有一条能让某个 `var` 的写排在一次读之前；每一条上写的入边都能追到那次读（`order` 直连，或经 JOIN/LOOP/GATE/语句 `sync`/`control` 传递）。

没试过的形状，写在这里免得下一个人以为是全覆盖：模块级顶层 cell 被函数捕获后的写、`sync` 块里再套 `sync` 块的读、对流的 `for` 里读同一个 cell、以及 `peek` 在语言层的其它入口（它今天只在生成器状态机里出现）。这四类我没有造出能跑通的探针，所以它们既不算证据也不算反证。

## 4. 复现

```bash
export LD_LIBRARY_PATH=$HOME/camel-eval/conda/envs/clang/lib
cd ~/camel-work/dedup
for f in /tmp/vo2.cml /tmp/vo3.cml /tmp/vo4.cml /tmp/vo5.cml; do ./out/latest/bin/camel $f std::jgir; done
# 然后在 JSON 里筛 kind 为 order/sync/control 的边，看 `:op/assn_l`（写）的入边
```

## 5. 这一轮没有改代码

没有可通过的反例，所以没有可加的东西：按「拒绝比错数好」，这一条不应当在没有反例的情况下再写一遍约束——再写一遍就是第二份判据。若下一步要动，入口是 §1 那处唯一的写点，不是别处。