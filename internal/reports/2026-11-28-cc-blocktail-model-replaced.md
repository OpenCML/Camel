# `cc-blocktail`：模型换了，不是冲突换了

**结论先行**：`lane/cc-blocktail` 的八笔（含一对互相抵消的 revert）建立在一套**已被尖端替换**的模型上，
换底时两个 `builder_expr.cpp` 的冲突不是"同一处的两种写法"，而是**两套模型**。
八笔没有可搬的东西 ⇒ **存档**（`archive/lane-cc-blocktail → 0286a8697e`）；但尖端**缺一条行为** ⇒ 另立 `task-366`。

## 1. 换底停在哪

```
树  D:\Projects\Camel-wt\compile-view    枝 lane/cc-blocktail    原 tip 0286a8697e
behind origin/integration 335    ahead 8
git rebase origin/integration → 停在第 1/8 笔 7054f6d43f
  UU src/compile/src/gct/builder_expr.cpp   （1 处：265-354）
  UU src/compile/src/gir/builder_expr.cpp   （2 处：941-963、967-1061）
  M  gct/tree.h · gct/tree.cpp · gir/builder_operators.cpp · 两个 editview 探针用例（已自动合入）
```

八笔：`7054f6d43f`（块尾提交它绑定的草稿）、`76c2b2e65b`（编译器自造的算子引用由编译器命名）、
`52665bc3af`（自造引用在体里不命名）、`fdce7c2b87`（块用块所在位置的类型作答）、`f2949d9f71`（有头块的值是其子）、
`1cc28d7aff`（view 块的值是体的一次调用，不是体的函数值）+ `48f02868e4`（Revert 上一笔）、`0286a8697e`（块头的名是块的根，按图读）。

## 2. 冲突 1：`gct/builder_expr.cpp` —— `visitBlockRoot` 在尖端已被删除

| 侧 | 内容 |
|---|---|
| base | 有 `visitBlockRoot`，DREF 标签 `isEdit ? ":block/open" : ":block/graph_of"` |
| **integration** | **整段没有**（`grep -c visitBlockRoot` = **0**）；`visitBlockHeaded` 只建零参闭包 + 编辑块的隔离边界，不读 `root()`、无 commit |
| 车道 | 保留 `visitBlockRoot`，标签改 `":" + uri`，并在 `visitBlockHeaded` 尾追加 `commitCall` + `markBlockValue(funcNode)` |

⇒ 车道要"**GCT 层**提交它绑定的草稿"；尖端是"**GIR 层**提交"（`gir/builder_expr.cpp` 里 `:block/commit` 包在隔离边界里），并且**不再有 BlockRoot 节点**。

## 3. 冲突 2：`gir/builder_expr.cpp` —— `resolveDRefNode` 尖端已重写

| 侧 | 内容 |
|---|---|
| base | `operatorGroupOf(*oper)` + `programExports`/模块名的长路径 |
| **integration** | **一行**：`internalOperatorGroup(*oper)` |
| 车道 | 在 base 上加"标签以 `:` 开头 ⇒ 去前缀查回算子 id"的兜底 + URI 拆模块名的长路径 |

⇒ 车道那两笔（`76c2b2e65b`/`52665bc3af`）**正是 `internalOperatorGroup` 一次性取代的东西**。
尖端 `internalOperatorGroup` 4 个文件命中；车道的 `markBlockValue` 尖端 **0** 命中，而 `isBlockValue` 尖端 5 个文件命中（那半边是尖端自己的）。

## 4. 三条判断

1. 那对 revert（`1cc28d7aff` + `48f02868e4`）**净效果为零**，可整对丢弃 —— 但丢弃**救不了这条枝**，剩下六笔建立在同一套已消失的模型上。
2. 第 1、2、4、5 笔与尖端"GIR 层提交 + `internalOperatorGroup` + 无 `BlockRoot`"**直接对立**；第 7 笔取决于 BlockRoot，尖端没有该节点。
3. **行为层面尖端已经是好的**：`body_return`、`body_fault_keeps_function` 在 `a22bc13e94` 上全绿。

## 5. 但尖端缺一条（这才是不存档的理由）

在 `343bac97ea`（`binary built 2026-11-28T00:20:03`）上跑五条：

```
[FAIL] read.root_observable_as_graph     ← 真缺
[PASS] read.root_observable_as_function  ← 尖端把 head 的名当函数，d(2.0) 能编
[PASS] read.body_return
[PASS] read.body_fault_keeps_function
[SUMMARY] total: 5, pass: 3, fail: 2
（第 5 条 edit_value_is_the_function 的红是入口写错 —— edit_value.cml 没有 main，不算判据）
```

真缺的那条的原文：

```
(root_observable_as_graph.cml:7:26) [Error]: No matching function found for argument types: <>
    ((x: float) => float pure).  Check the number and types of …
    <graph:nodes>: (d: Graph) => Node[]
 (name=SemanticDiag::NoMatchingFunction, code=0x0300000F)
```

复现就是设计第 4 节的形状：

```camel
func f(x: float) => float { return x + 1.0 }
func main() => int sync {
    let c = view d = f { nodes(d) }     // 设计：块头的名是"这个块的根"，读成 Graph
    return 0
}
```

⇒ **该是句柄的地方是函数，该是函数的地方反而能编** —— 两条正好对调。这与 `task-350` 量到的 a)/c)/d) 是同一个缺陷。
⇒ 立 `task-366`：在尖端的新模型里把**头名字绑定到"块的根"**，且**不许**把 `d(2.0)` 变成合法调用。

## 6. 处置与读数

```
git rebase --abort        → 回到 lane/cc-blocktail @ 0286a8697e，status 空
git checkout --detach 0286a8697e
git tag archive/lane-cc-blocktail 0286a8697e
git branch -D lane/cc-blocktail   → Deleted (was 0286a8697e)
读数：status 空 · tag = 0286a8697ea8209d3e9cc97e3578ed883088ee1c · branch 已删 · 树干净
```

## 7. 三句净值

```
· 模型换了、不是冲突换了
· 八笔没有可搬的东西
· 但缺一条行为：该是句柄的地方是函数、该是函数的地方反而能编
```

**教训**：一条落后 335 笔、带一对 revert 的枝，先问"它建立的那套模型还在不在"，再谈冲突怎么解 ——
模型不在了，冲突就无从解起，硬解只会把尖端退回去。