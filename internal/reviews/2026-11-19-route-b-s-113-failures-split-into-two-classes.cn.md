# 路 B 的 113 条红分成两类

**日期**：2026-11-19
**提出者**：车道 N（`lane-ssa`，基线 `3a5efdd0d`）
**状态**：待裁（两节各写死一条默认建议，本车道按默认开工）
**触发**：按作者 2026-11-19 的路 B 做 S4b，补丁态 `linear` 113 红。两类出自同一件事——把一个带 `var` 形参的函数的**出口类型打包**——所以写成一页两节，不写两页：分成两页会让下一个人以为有两个独立的问题。

## 1. 三个判据，越「对」越红（同一把尺子，刷新后的基线）

| 判据 | 红 | 说明 |
| --- | --- | --- |
| 按**签名**判（`packsVariableParameters`） | **184** | 正好等于**完全不打包**的基线 ⇒ 这个判据一次也没成立 |
| 按**形状**判（元组份数 = `var` 形参个数 + 1） | **101** | 打包出来的元组与用户写的元组形状相同 ⇒ 两者混在一起 |
| 按**位**判（`GraphDraft::exitTypeIsPacked`） | **113** | 位真的分开了两者，于是把以前被盖住的 **53 条泛型**分了出来 |

**「更对」不等于「更少」**：一个更好的判据会暴露更多红，因为旧的判据把它们盖住了。所以 113 不是退步，是这 113 条第一次被分开看见。

## 2. 第一节：尾调用（44 条）

**诊断原文**（`test/cases/linear/byref/var_param_reference.cml:48:12`）：

```
Return type mismatch between branches (in function 'count: (var acc: int, n: int) => int'):
expected 'int', found '(int, int)'
```

取值路径换一条，报的就是运行期那一句：`Maximum recursion depth (1000) exceeded when evaluating graph 'count'. A call in tail position takes no frame ...`。最小用例就是这段 `count`：`return if n <= 1 then 0 else count(acc, n - 1)`——被调者交回元组，那一支的值必须是元组第一份，于是调用之后多了一个节点，而尾调用就是「调用之后没有节点」。成员：`var_param` 33、`var_writes_through_recursion` 6、`var_read_before_callee_write` 5。

**默认（写死）**：**自调用的图不打包**，保留老协议（`var` 形参仍是共享格子）。尾调用是这段程序写没写对的问题，不是实现细节；抬深度是把语义藏进一个更大的数，改用例是把判据删掉。

## 3. 第二节：泛型（53 条）

**诊断原文**（`test/cases/modules/autodiff/var_through_with_grad.cml:26:25`）：

```
Invalid call to 'with_grad' with argument types <m: P> (x: float[...], var cnt: int) => (float, int)
uses var cnt: an operator calls a function argument with values, and a function with a var parameter
must be given a variable.
```

读者读到的是**被打包过的出口** `=> (float, int)`，而它的声明是 `=> float`：泛型函数被当值传进算子，算子按「有 `var` 形参就要给变量」检查，于是拒绝。成员：`var_grad` 33、`var_rule_subpasses` 20。

**默认（写死）**：**泛型函数的 `var` 形参仍走 cell，不打包出口**。理由同上一条：不许把语义问题藏进一个更大的数。**若它能打包**，要找的是「泛型实例化」与「出口类型」能同时成立的那一处——那在 `src/compile/gir/` 的类型定义里，**不在本车道的权限里**，届时要请作者裁。

## 4. 两节共同的规矩

- **这一类的红在 `--update-golden` 之前不许动**：15 条 `:var/take` 图金标钉的是今天的行为，在打包判据定下来之前更新它们，等于把待定的形状烤进钉子。
- **不许在合并范围里二分**：`96de9a00d`（单提交回退后仍 101）与 `768a6476e`（回退后仍 113）都已排除，剩下的不是某一笔合并 commit 的问题。
- 读数与基线：`linear` 869 / 113 红（本节三行）、树 `3a5efdd0d` 上干净态 869 / 864 / 0 / skip 2 / known 3；补丁在 `D:\Temp\ssa-s4b-bit.patch`。

## 默认建议（写死）

**默认建议**：按 §2、§3 已经写死的两条开工 —— **自调用的图不打包**（保留老协议，`var` 形参仍是共享格子）、
**泛型函数的 `var` 形参仍走 cell、不打包出口**；落地后按 §1 的第三把尺子（`GraphDraft::exitTypeIsPacked`）
重跑并**分两行报**：尾调用 44 条降到几条、泛型 53 条降到几条，**不许合成一个数**（§1 的教训就是
「更对」会先看起来「更多」）。
**判据**：`-Plan feat/linear -Jobs 8` 的 fail 从 113 降到只余这两条覆盖不到的条目，且读数里
不许出现 `Maximum recursion depth (1000) exceeded`（那是尾调用被拆的签名）；`--update-golden` **之前**
不动那 15 条 `:var/take` 图金标。

## 决策者答（2026-11-23）：被重构取代

两个默认（自调用不打包、泛型保留 cell）都违反 3.5，作废。这些页出自同一个根子：GIR 建图器在一个阶段里同时定类型和建图，并用 cell、打包位、就地改开放签名来表达 `var` 形参。核心抽象第 8 节定了前端分阶段：类型检查阶段"函数类型在这里定下来，此后不再改"；建图"只做机械的翻译……`var` 只降低成 SSA（原则 3.5），`Ref` 是唯一的单元"；第 5 节定了"函数类型有唯一的主人"，第 17 节定了"'开放类型'随前端重构消失"。于是本页问的机制在新前端里不存在：调用表达式的值永远是声明的结果，`var` 形参的终值是调用的另一份输出，由调用处重绑。lane-ssa 分支不合，归档。

带进 前端分阶段 的验收输入：页中 113 条失败的两类用例（含 `modules/autodiff/var_through_with_grad.cml:26`：泛型或变换接收带 `var` 形参的函数，按声明类型检查）
