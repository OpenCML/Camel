# 「这个 sync 块没人用」这句警告，今天还响得了吗？

日期：2026-11-03　状态：**默认按"不可达"处理，准备删；不自行删**

## 问什么

`SemanticDiag::EffectNotExecuted` 的两种措辞里，一种是
`the sync block it is in is not used`（`src/compile/gir/builder_diagnostics.cpp#Builder::reportDroppedEffects`），由
`inUnusedSyncBlock(start)`（同文件 `478–507`）返回 `true` 时选中。问题是：**在当前模型里，
有没有一个程序能让它返回 `true`**？

## 三种形状与读数（都在 `c9bc77045`/`d3d70c373` 的二进制上跑，`std::nvm`）

| 形状 | 程序 | 读数 |
| --- | --- | --- |
| 未使用的 `if … then sync … else sync …` 值 | `let unused = if x > 0 then sync { println("side") 1 } else sync { 2 }` | 打印 `side`，`EXIT=0`，无警告 |
| 同一形式当语句 | `if true then sync { println("side") 1 } else sync { println("other") 2 }` | 打印 `side`，`EXIT=0`，无警告 |
| 从未被调用的 `sync` 函数 | `func f(): void sync { println("in-void-body") }` + 空 `main` | 无输出，`EXIT=0`，无警告 |

另外试过但**语法不接受**的：`let x = sync { … }` 与 `let x = (sync { … })`（`Unexpected 'sync';
expected an expression`）、`do { … }`（`Unexpected 'do'; expected '}'`）。也就是说 `sync {` 只能出现在
`then sync {`、`else sync {`、`=> sync {` 与函数体 `sync {` 这几个位置。

## 结论

- `test/` 树里**没有任何用例**钉住这句话（`grep "sync block it is in is not used"` 在 `test/` 零命中；
  只有本页与另外几页的正文提到它）。
- 加上本分支回退了 `a49cac54d` 的链尾位门之后，三种能写出来的形状都不响。

⇒ **默认判定：这段在当前模型里不可达**，而一段不会响的守卫比没有守卫更糟（它让人以为这件事有保护）。

## 删之前要动的清单（**未执行**，等合并车道带作者裁）

1. `src/compile/gir/builder_diagnostics.cpp`
   - `478–507`：`inUnusedSyncBlock` 这个 lambda 本身；
   - `540–542`：`inUnusedSyncBlock(id) ? "the sync block it is in is not used" : "nothing depends on it
     and it is not in a sync block"` 那一处**只保留后半句**；
   - `550`、`570` 两处注释提到它，随句子一起改。
2. `src/compile/gir/builder.h#Builder`、`:415` 的注释里写着这条诊断的口径。
3. `docs/design/effects/plan.cn.md:75` 说的是 `EffectNotExecuted` 的判据（**不要整条删**：这同一个诊断码还承载
   "nothing depends on it and it is not in a sync block" 那一支）。
4. `docs/reference/camel-for-ai-diagnostics.md` 与 `docs/design/effects/04_execution_model.md:50/65/128`
   引用的是 `EffectNotExecuted` 的另一支（`println(x)` 那种），**不在删除范围**。
5. 会变的测试：**没有**。没有任何用例钉住这句原句，删掉它不改任何期望；风险只在于将来某种图形形状
   不再被报出来——这正是需要作者裁的地方。

## 仍欠的测量

P0-2 的三个数（`place_nowait`、`GATE` 计数、链位数）在回退之后**还没重测**。探针在
`D:\Temp\p02-probe-site.patch`，重打一次即可；本页写"欠一次重测"，不作结论。