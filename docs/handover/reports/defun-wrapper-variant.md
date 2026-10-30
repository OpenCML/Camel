# 去函数化的包装变体：遍自己写的图

遍第一次自己造图：id 取自 `Context::graphIds`、`PORT` + 直接调用 + `EXIT`、`encode`/`adoptGraph`；无人引用，故提交时被丢下，程序照跑。

这一页记两件在代码里看不见、却会在下一笔直接决定红绿的要求。

## 一、`FUNC` 节点的被调图必须同时进图的引用表

`addFuncNode` 只把被调图写进 payload 表（`GraphDraft::payloadGraphIndexOf`），而编码器是按**图的引用表**给 payload 里的那个下标定名的——引用表就是那四个列表（`addDependency` / `addSubGraph` / `addStaticGraphRef` / `addRuleGraphRef`）合起来的结果。只进 payload 表，第一次整闸红在这一句：

```
FUNC node 1 calls a graph that is not among the graphs this graph calls (its reference table).
```

`src/passes/opt/defun/defunctionalize.cpp#writeCallGraph` 因此把被调图也用 `addDependency` 加进引用表。下一笔里真被引用的图（包装图）走的是同一条路，而且是**两端都要**：

* 造它的遍，在它自己的草稿里 `addDependency`；
* 被改写去调用它的那些图的草稿，也要 `addDependency(包装图的 id)`——否则同样是这一句红，而这次它是在**一次真的改写之后**才出现，很容易被当成改写本身写坏了。

## 二、借一次的手不许点名类型

写图 helper 先写成 `GraphDraft &draft = transaction->edit();`，账本（`test/cases/runtime/graph_draft_ledger`）当场判红：files 66 → 67、references 535 → 536。它只借一次草稿、并不持有它，所以 helper 收的是模板参数（`template <class Draft>`，从调用点收 `transaction->edit()`），三个数回到 66 / 535。

这一条不是"台账省一格"，是账本 §15 的规矩在 b4 之外第一次应验：**在代码里写出 `GraphDraft` 就是宣告自己持有它**；只想借的写 `auto`，或者写模板，让所有者留在事务／会话那里。

## 这一笔的读数

整闸 `total 7903 / pass 7892 / fail 0 / skip 9 / known 2`；defun 计划 18/18。三个数未动（这一笔只加能力、不删拼写）。