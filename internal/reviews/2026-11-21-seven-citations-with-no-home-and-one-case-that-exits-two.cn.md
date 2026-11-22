# 七条引用没有落点，以及一条退 2 的新用例

**2026-11-21 17:15，树 `193bb2fed`。这两件事都不是语义问题，但都挡着 `docs.links.citations` 与
`docs.headers` 那一族在四条腿上的绿。**

## 一、引用：28 条里 21 条已经修好，剩 7 条

`node scripts/check-doc-links.mjs --citations` 从 `28 unverified` 降到 `7`。修掉的是「文件被拆走、
符号还在」的那一类——按**定义处**找的，不是按名字猜的：

| 符号 | 现在指向 |
| --- | --- |
| `buildTransposeMember` | `modules/transforms/transpose/walk.cpp`（`transpose.cpp` 已拆成目录） |
| `ruleTakesOperands`、`chooseTransposedRule` | `modules/transforms/transpose/rules.cpp` |
| `transposeSignature` | `modules/transforms/transpose/signature.cpp` |
| `Builder::visitGeneratorBody` | `src/compile/gct/builder_func.cpp`（`builder.cpp` 已拆成六个） |
| `visitArrayType`、`dimBindAllowed_` | `builder_type.cpp` / `builder.h` |
| `MacroCmp`、`DimPack`、`FoldCountsReport` | 各自落到的那个文件 |

`internal/` 下的历史页我**没有动**：那里写的是当天那个文件的路径，改它等于改记录，
不是修引用（检查器也不读那里）。

### 剩下的 7 条：符号按旧名字已经找不到定义了

| 位置 | 引用 | 现状 |
| --- | --- | --- |
| `docs/design/arrays-memory/plan.cn.md:284` | `dimBindAllowed_` 已修 | — |
| `docs/design/effects/plan.cn.md:50` | `builder_expr.cpp#core` | 两个 `builder_expr.cpp` 都不含 `core` |
| `docs/design/graph-model/README.cn.md:100` | `src/compile/gir/builder_operators.cpp#FUNC` | 文件在，不再出现 `FUNC` |
| `scripts/law-cotangent-ports.mjs:317` | `modules/transforms/transpose.cpp#ruleCotangent` | 只有调用点，没有定义 |
| `src/compile/gir/builder_generator.cpp:81` | `src/compile/gct/builder.cpp#equalTo` | 全仓搜不到带括号的定义 |
| `test/cases/compile/transforms/matmul_transpose_vector_lhs_stated_extent.cml:24` | `#shapeFromType` | 同上 |
| `test/cases/compile/transforms/matmul_transpose_vector_rhs_stated_extent.cml:15` | `#shapeFromType` | 同上 |
| `test/cases/compile/transforms/transpose_refuses_a_parameter_value_it_cannot_be_handed.cml:10` | `transpose.cpp#…` | 文件已不存在 |

**这七条我需要一个名字**：`core`、`FUNC`、`ruleCotangent`、`equalTo`、`shapeFromType` 现在各自
叫什么、在哪个文件。它们是**改名**不是搬家，只有拆文件的人知道新名字；我不按相似度猜，
猜错比留着更糟。

## 二、新用例 `transpose_refusal_is_one_cause_in_four_places` 退 2

四条腿都红，读数：

```
expected exit 0, got exit 2
stdout does not contain 'every cause has one word, one sentence, and a site that records it'
```

`exit 2` 是这个检查器自己的用法错误（不是被测程序拒绝）。请确认它是脚本参数问题还是用例写错，
修好后这条会和其余一起进下一轮读数。
