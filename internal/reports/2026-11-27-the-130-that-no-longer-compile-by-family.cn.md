# 第 2 类（“用例不再能编译”，130 行）归族表

来源：`internal/reports/2026-11-27-the-reds-by-cause.cn.md` 的附二。数据 = `D:\Temp\t122\fail\<plan>\<test>\meta.json`
（tip `065bfb1690`）。本页只读、零 run。

## 两个计数口径，别混

```
130 行  = `diagnostics[0].name` 以 `SemanticDiag::` 开头 且 `diagnostics[0].path == meta.case_path`
 67 个  = 其中**不同用例文件**数（去重）
 33 个  = 不同诊断名
```

**⇒ 一处必须记住的字段事实**：`meta.json` 的 `diagnostics[]` **只有结构化字段**
（`path` / `line` / `column` / `name` / `code`）—— 472 份带诊断的 meta.json 里**没有一份**有 `message` 字段。
所以“按诊断正文数任何一组”必然是 **0**，那是字段形状，不是没有这一类。**读诊断正文只能去 `output.txt` / `stderr.txt`。**

## 归族（按 tests / cases 双计数）

| 族 | tests | cases | 诊断名 / code | 一句 root cause | 判类 |
|---|---|---|---|---|---|
| ① | 31 | 12 | `NoMatchingFunction` 0x0300000F | 重载集里没有一个能接受实参的静态类型（泛型/参数化声明与新实参形状不匹配） | 实现缺陷（同一形状反复） |
| ② | 14 | 3 | `WhenConditionUnmet` 0x030001AC | `when` 子句的条件在该调用点不可满足 | 需设计一句（约束推导边界） |
| ③ | 12 | 4 | `GenericArgumentsMismatch` 0x03000053 | 显式泛型实参个数/种类与声明的形参表不一致 | 用例过时（写法未跟声明） |
| ④ | 11 | **1** | `ReturnTypeMismatch` 0x03000011 | `content_equality.cml` 经 fold/inline 后返回类型与声明不符（`0 .. n` 当 `int[]`） | 与 `D-1123-41` 同一片，**暂不派修复** |
| ⑤ | 6 | **1** | `LoopBodyTypeMismatch` 0x03000121 | 循环体被取作状态分量的 `var` 多算了一个（见下节） | 实现缺陷 |
| ⑥ | 5 | 2 | `EmptyArrayWithoutType` 0x030000B2 | 空数组字面量无法定型（设计 5.1:374 要 `[] as T[]`） | 用例过时 |
| ⑦ | 5 | 5 | `ArgumentNeedsConversion` 0x03000021 | 实参需要隐式转换而该位置不做转换 | 需设计一句（隐式转换规则） |
| ⑧ | 4 | 3 | `NeverArgumentCall` 0x031001C0 | 以 `never` 值作实参调用 | 实现缺陷 |
| 其余 | ≤4 各 | — | `UnusedFallibleValue` / `PartialApplicationRefused` / `KnownNotNull` / `UnresolvedReference` / `EffectInRuleCondition` / `ReturnNeedsConversion` 等 | 各自一条一句 | A/B 混合 |

**Range 口径更正**：原报告写“消息正文里直接出现 `Range` = 67 行”，实测按诊断正文是 **0**（字段形状，见上）；
可读的证据是**失败输出目录的文本**里出现 `Range`，那才是 **67 行**。它与“不同用例文件数 67”**同值是巧合**。

## ⑤ 的钉点（已到行）

```
用例：test/cases/linear/runtime/var_read_before_region.cml:32
      loop i = 0 while i < 2 do sync { z = z + 1; i + 1 }        （`z` 是循环外的 `var`；文档要求输出 `0 2`）
6 个测试 = 该文件 {default, fvm, jit, nvm, opt_nvm} 5 个后端组合 + 1 个 poison 副本 ⇒ 不是 6 个缺陷
诊断（output.txt 首行，`:32:9`）：
  The body of this loop gives a value of type 'int'; the state of the loop has type '(int, int)'.
  The initial state fixes the state type. … (name=SemanticDiag::LoopBodyTypeMismatch, code=0x03000121)

断口：src/compile/src/gir/builder_control.cpp:3433-3439
  for (size_t j = 0; j < carried.size(); ++j) {
      node_handle_t next = carried[j].port;
      if (const auto written = loopStateWrites_.find(carried[j].port); …) next = written->second;
      components[declaredStates + j] = next;      // ★ 被携带的 var 追加成第 declaredStates+j 个状态分量
  }                                               // :3381 components(stateCount, nullptr)；:3379 if (!carried.empty())
规则来源：同文件 :2339「a write inside a loop is carried state…」⇒ `var` 在循环体里被写就进 carried
⇒ i = 0（declaredStates=1，int）+ z（carried，int）⇒ stateType = (int, int)
  而 body 交回 i + 1（int）⇒ :3495 assignableFrom 失败 ⇒ :3499 报 LoopBodyTypeMismatch
```

**与 `buildStreamLoop` 的关系**：同文件、同一族症状，但**是两处**：
`:3983-3984` 是流循环把**整个**状态类型当成状态类型用；`:3433-3439` 是 while 路径把 carried `var` 追加成分量。
⇒ 判“同一处还是两处”要两处的 `file:line` 并列，**诊断名相同不算同处**（`hasPath`、`console` 两次教训）。

## 排序（条数 ÷ 修点）

```
④ ReturnTypeMismatch   11 tests / 1 case   ← 最高，但归 `D-1123-41` 一片，暂不派
⑤ LoopBodyTypeMismatch  6 tests / 1 case   ← 次高，已钉到行，可先派
① NoMatchingFunction   31 tests / 12 cases ← 条数最多，要 12 个修点
```