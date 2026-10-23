# `bf16`/`f16` 值转成文字，是"该类型里最短且读回同一个值"，还是"它精确保留的那个 f32 的最短文本"

出处：`docs/handover/reviews/reduced-precision.md` 第 (a) 条"打印按 11.5 的浮点文本规则"；实现分支 `handover/h330-bf16-scalar`。

## 现状（实测）

`println`（`src/core/rtdata/print.cpp:108-112`）与 `HalfData::toString()`（`include/camel/core/data/primary.h:131`）都先把 bf16/f16 的槽按 `halfToFloat` 无损展成 f32，再交给 `camel::utils::floatText<float>`。也就是说，打印用的是 **f32 的规则**：

```
println(0.1 as bf16)   ->  0.100097656      // bf16 0x3DCD，展成 f32 恰好是 0.10009765625
```

`include/camel/utils/number_text.h:454-464` 把规则写死为"**该类型**里最短、且读回同一个数"，并举例说明 f32 打印 `0.1` 而不是 `0.10000000149011612`。`docs/design-principles.cn.md` 7.7 的原话是"浮点数转成文字只有一种规则：最短、且读回来得到同一个数"，11.5 把它指到 7.7。

两种读法在当前实现下都自洽：

- **读法 (i)（按值自己的类型）**：bf16 0x3DCD 的"同一个数"是那个 bf16。`0.1` 作为 bf16 字面量读回的正是 0x3DCD，所以最短文本是 `0.1`；今天打印的 `0.100097656` 不是最短的（它读回同一个 bf16，也读回同一个 f32，但更长）。
- **读法 (ii)（按它精确保留的数值）**：bf16 槽里的数就是它展成的那个 f32 0.10009765625，因此"同一个数"是这个 f32，最短文本是 `0.100097656`——这正是今天的输出。

两者都能通过"读回来得到同一个数"：`0.1` 与 `0.100097656` 读成 bf16 都得 0x3DCD。

## 问题

`bf16`/`f16` 的文本，取 **(i) 该类型里最短**（`0.1`），还是取 **(ii) 它展成的那个 f32 的最短**（`0.100097656`）？

## 选项与代价

**(i) 按值自己的类型求最短。**
收益：与 f32/f64 逐类型成立的规则一致（7.7 与 `number_text.h` 的措辞都按类型说）；`println(x)` 的文本与"同样的数写成 bf16 字面量"一致，读程序的人看到的就是能写回去的形式。
代价：要在 `src/core/rtdata/print.cpp` 一侧为两种类型各写一次"逐精度试到读回同一个元素"的循环（bf16 至多 4-5 位有效数字、f16 至多 5 位），`floatText` 的排版规则要抽出来复用；`HalfData::toString()`、`format`、字符串插值、复数部分、`--inspect` 的常量文本若都按此规则，改动面比看上去大（它们今天都走 `floatText`）。另外同一个数值用 bf16 与 f32 打印会得到同一个短文本，这是逐类型规则的必然结果，不是缺陷。

**(ii) 保持今天的行为，把规则写明为"展成 f32 后按 f32 求最短"。**
收益：零改动；文本读成 f32 恰好是那个 f32，读成 bf16 也回到同一个元素，两种读法都对；不需要为两种类型各写一次最短化。
代价：与 7.7 的字面（"最短"）在"最短于哪个类型"上有解释空间；`0.1 as bf16` 打印出 9 位数字，看上去比写下的多，容易被当成 double rounding 的痕迹。

## 倾向

**(i)**：7.7 说的是"浮点数转成文字"，bf16/f16 是语言的浮点类型，其"同一个数"自然是它自己的值；`number_text.h` 的注释也是逐类型陈述的，说明这条规则本来就不是"一律按 f32"。本分支目前按 (ii) 落定，并把实测文本钉在 `test/cases/modules/stdlib/reduced_scalar.cml`（`0.100097656 205 61` 一行），注释指向本文件；若裁定为 (i)，只需改这一行期望与打印实现，用例其余部分都是字节比较，不受影响。

## 判断在谁

语言作者（决策者）。判断依赖"7.7 的'同一个数'相对哪个类型"，属语义/输出文本，不由实现者决定。

## 来源

- `docs/handover/reviews/reduced-precision.md` 第 (a) 条
- `docs/design-principles.cn.md` 7.7、11.5
- `include/camel/utils/number_text.h:454-464`（`floatText` 及其规则说明）
- `src/core/rtdata/print.cpp:108-112`、`include/camel/core/data/primary.h:131`
- `include/camel/core/rtdata/half.h`（`halfToFloat`，无损展宽）
- `test/cases/modules/stdlib/reduced_scalar.cml`（实测文本）、`test/plans/feat/modules/stdlib_reduced.plan.toml`