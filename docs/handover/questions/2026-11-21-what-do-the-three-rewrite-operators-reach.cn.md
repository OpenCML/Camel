# 三个改写算子各读写什么资源？

**本页问一件事**：`rewrite:saturate`、`rewrite:check`、`:mark/foreach_arr` 这三个算子各自实际读写哪些资源。它们的声明里读不出来，于是 `beyondCoverage()` 按保守读取拒绝它们建的图，四个用例因此红。

## 现状（读数）

`src/runtime/resources.cpp:1397` 把 `ResourceClass::Unknown` 的访问与其它未声明覆盖的访问一起判进 `beyond`（`:1403-1406`），原因句在 `unnamedReasonOf()`（`:1420`）的两支：`:1433` 算子根本没有声明、`:1436` 声明里读不出资源。落到提交检查是 `effectBeyondDeclaration()`（`:1766`）。

四条红的原文：

```
node 9 of graph 'main' (rewrite:saturate), in a graph whose type declares uses var stdout,
  makes an access no clause can name (write unknown): 'rewrite:saturate' declares no resources,
  so every resource its operands may name is read conservatively

node 5 of graph 'main' (rewrite:check)   -- 同形

node 1 of graph '__root__' (a call) declares uses var stdout; the graph it runs touches
  any unknown; no clause can name what node 5 of graph 'main' (:mark/foreach_arr) reaches:
  its declaration names no resource the analysis can read
```

⇒ 两个算子**压根没声明**（`semantics` 为 `nullptr`），一个有声明但**没点名任何资源**。

## 为什么这不是检查的错（依据）

这条红线是**恢复**，不是新增：

```
cbf9862ea (11-20) 引入      → 20 个 gpu 用例变红
fcf2dd716 / 9dd1e65dd      → 把那四个文件从 develop 摘掉
dc0f73b8f                  → 让 tiles 那些算子声明效应，20 个用例转绿
b993eeaaf (15:37)          → 原样放回（commit message:「The four files are b838bbe91's copy,
                             restored verbatim: no wording is re-decided here」）
```

同一笔给 `docs/design/effects/analysis.cn.md` 写的是「**说不出资源的访问同样拒绝**（负责人 2026-11-20）」，并写明「声明自己写了 `any unknown` 的照旧覆盖它」。**它一个 `test/` 文件都没碰。**

⇒ 所以修法在**声明侧**：与 `dc0f73b8f` 给 tiles 做的事同一个动作。

## 要作者定的

**这三个算子各自实际读写什么？** 我读不出来，而猜一个声明会把红线变成橡皮章。

## 默认建议（没回复就按这个开工）

**给三个算子各自的声明补上它真正读写的资源；补不出来的那一个，在计划里记为 `known_fail` 并写明结束条件「当 <算子> 的声明点名了它读写的资源」。**

具体到每一个：

| 算子 | 默认建议 |
|---|---|
| `rewrite:saturate` | 它把规则库饱和后交回图，**读规则库、写图**；按这个补声明。若它有 `with` 参数，参数声明的资源也照抄 |
| `rewrite:check` | 只读不改：**读它检查的那张图**，不写任何运行时资源 |
| `:mark/foreach_arr` | 它读写**它的参数数组**；按参数的元素类型补，不写 `any unknown` |

**不做的事**：不给它写一句 `any unknown` 了事 —— 那句话会把它管不到的资源也一并认领，红线从此对这三个算子失效。