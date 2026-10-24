# `static loop` 的序列：裁定写"都行"，今天的编译器只认三种

（h471-doc-debts 车道新增，2026-10-25。裁定原文见
[`../reviews/questions-2026-10-24-evening.md`](../reviews/questions-2026-10-24-evening.md) 的
§h462-static-loop-sequence-boundaries 第 2 条。）

## 实测（88 主机，`183c3e479` 的构建，`camel <file> std::nvm`）

| `in` 后的写法 | 结果 |
|---|---|
| `0..3`（常量区间） | 接受，打印 `3` |
| `[1, 2, 3]`（数组字面量） | `SemanticDiag::StaticLoopSequenceNotKnown`：`'[1, 2, 3]' is not.` |
| `range(0, 3)`（常量实参） | `SemanticDiag::StaticLoopSequenceNotKnown`：`'range(0, 3)' is not.` |
| `ts`（剩余参数的编译期元组） | `SemanticDiag::StaticLoopSequenceNotKnown`：`'ts' is not.` |
| `fields T`、`indices ts` | 接受（既有用例与 `docs/language/06_expressions.md` §6.8.4/§6.8.5 的示例） |

四个自包含的探针程序在 `D:\Projects\Camel-wt\notes\h471-probes\`（`probe_interval.cml`、
`probe_array.cml`、`probe_range.cml`、`probe_tuple.cml`），本车道未把它们入库。

## 影响

- 裁定第 2 条把 `range(a, b, step)`、数组字面量、常量数组、编译期元组都写成"都行"；实现今天只认
  `fields T`、`indices ts` 与常量区间 `a..b`，其余按 `StaticLoopSequenceNotKnown` 拒绝。
- 文档按"文档随代码走"只写了实现认的这三种：`docs/language/06_expressions.md` §6.8.5、
  `docs/reference/camel-for-ai.md`、`docs/language/08_top_level_and_grammar.md` 的生成文法块
  （`staticLoopHeader : 'for' (identDef | '_') (',' (identDef | '_'))? 'in' ('fields' typeExpr |
  'indices' dataExpr | dataExpr)`）与诊断目录的消息都是这个读法。若把裁定第 2 条直接写进语言参考，
  页面会教今天编不过的写法。
- 需要作者/负责人定一条：按第 2 条补实现（那时再改文档），还是把第 2 条的措辞收成"今天只认常量区间"。
  这不是文档欠账，本车道没有替它做决定。