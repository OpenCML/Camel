# 问题：转置走图还按操作数个数拒一个算子吗？

**日期**：2026-11-04
**状态**：**已量到，不是待裁**。这一页是"文档记着已经消失的事实"那一类，看到旧话就当场改，并重生成参考页。

## 1. 结论

**不按个数拒。** `TransposeRefusalCause`（`modules/transforms/transpose.h`）的成员里**没有 `Arity`**，`modules/transforms/transpose.cpp` 里也**没有 `skip=arity` 这个发射点**；原来个数守卫那处的注释写明它被换成了别的机制。所以"一个算子最多两个普通操作数，第三个位置会在找律之前被拒"**不是当前事实**。

## 2. 为什么这件事值得单独一页

1. `stdlib/tensor.cml` 里 `sum_stated` 那一段的导言原本写着：范围放在一个 `int[]` 里，**因为转置走图只认一两个普通操作数，第三个位置会在找律之前被拒**。我做 `max_axis_stated` / `min_axis_stated` 时需要第三个端口（并列掩码），才发现这句已经过期；改成了"范围共用一个列表是因为反向律要整份读它"，并注明两个极值算子就带第三个端口（提交 `62336cdbc`）。
2. 过期那句**直接误导设计**：读者会据此把两份互不相干的信息塞进一个 `int[]`，或者断言"掩码这种第二份数据根本带不上去"。我这一轮最初就差点因此走成"把并列标记编进 `at`"。
3. **参考页是跟着源注释走的**：`scripts/stdlib-reference.mjs --write` 会把那段 `//` 导言抄进 `docs/stdlib-reference.md`。所以改注释**必须重生成**，否则文档继续传播旧事实（我改完跑了 `--write`，条目数 542 不变、导言那段跟着变了；`docs/reference/camel-for-ai-stdlib-signatures.md` 的生成区读的是参考页，所以要再跑 `node scripts/camel-for-ai.mjs --write`）。

## 3. 读数

- `modules/transforms/transpose.h#TransposeRefusalCause`：枚举成员无 `Arity`。
- `modules/transforms/transpose.cpp`：全文件无 `skip=arity`；个数守卫处留的注释说的是"换成了按律的端口形状判"。
- `max_axis_stated(t: T[..], mask: bool[..], at: int[]): T[..]` —— 三个普通端口，律照常按位置实例化类型参数；`test/plans/feat/compile/stated_extents.plan.toml` 的两条并列/负轴用例两腿都绿（提交 `3d95855c7`）。

## 4. 默认

**任何"第三个端口会被拒"的设计前提都不成立**：需要几个端口就写几个，把"这条信息带不上反向半段"的判断**先量再信**。旧文档这么写时当场改注释、重生成两个文档、并在提交信息里写清"旧话已不成立"。