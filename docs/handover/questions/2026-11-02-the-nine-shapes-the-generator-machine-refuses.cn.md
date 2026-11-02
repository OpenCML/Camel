# 生成器拒绝的九种形状：哪些能从源码走到

> **该页引用的「本版本尚未构建 / 派内部错误并把程序发来」一句已于 2026-11-02 由作者裁定作废**（生成器 `loop` 的计算初始值是合法程序）。以下保留当日原文与当日读数。

**日期**：2026-11-02。**只读一轮**（不改代码、不跑全量）。目的：决定"加一个语义码还是加三个"。

## 1. 九个调用点与各自的守卫条件

| # | 行号 | 守卫条件原文 | 能否从 `.cml` 源码走到 |
| --- | --- | --- | --- |
| 1 | `6001` | `if (values->size() == 0)`（在 `stmtType == Exit && exitType == Yield` 之下） | **不能**：裸 `yield` 是语法错误（`SyntaxDiag::UnexpectedToken: expected an expression`） |
| 2 | `6105` | `if (!isIf && !isMatch && !isLoop)` 里的 Block 分支 | 需一个机器不构建的语句形状，未实测命中 |
| 3 | `6126` | `if (seqAst == nullptr || optAtAs(2) != nullptr \|\| state().size() != 1 \|\| init == nullptr \|\| ...)` | **不能**：`loop for v in ...` 在源码层是 `SyntaxDiag::KeywordAsIdentifier: 'for' is a keyword` |
| 4 | `6216` | `if (inits->size() == 0 \|\| state().empty() \|\| hasFor() \|\| inits->size() != state().size())` | 未实测命中（`loop for` 也被语法挡住） |
| 5 | `6228` | `if (!hoistable(eachInit))`（**嵌在 #4 的条件体里**） | **能，已实测**（见 §2） |
| 6 | `6303` | `match` 的 `pattern` 为 `DataType::Ref`/同类节点 | 未实测命中 |
| 7 | `6376` | `if (last == nullptr)`（切分点前没有语句） | 未实测命中 |
| 8 | `6412` | `if (tupleItems == nullptr \|\| tupleItems->size() != names.size())` | 多值 `yield` 多在上游被判（`SemanticDiag::DeclaredTypeMismatch`） |

（`9001` 一行的 lambda 定义在 `:5717`，九个"调用点"含它；真正 `reportUnbuiltShape(...)` 的调用是上面八个。）

## 2. 唯一实测命中的形状与它的最小程序

```cml
func f(): int {
    return 0
}

func g(): int.. sync {
    loop i = f() while i < 3 do {
        yield i
    }
}

func main(): int sync {
    return 0
}
```

诊断原文：

```
error[InternalDiag::UnknownInternalError]: An error has occurred: the generator machine does not build this shape; send the program, this is a compiler defect.
  --> /tmp/probe/g.cml:7:9
```

**要点**：`loop i = f() while ...` 的**初始化表达式是一次调用**（不可 hoist），语句是合法的、类型也是对的，
**所以这不是"程序写错了"，而是机器缺一种形状** —— 正是作者说的"要么实现，要么报一个说明语言规则的诊断"。

## 3. 哪几个 `if` 是同一个条件

- **#4 与 #5 是同一个 `if` 块**：`6228` 嵌在 `6214` 的条件体里面，两者是同一次判定下的两种失败（数量不对 / 初始化不可 hoist）。
- **#7 与 #8 是同一处结构（切分点前的最后一条语句）** 的两个失败：没有语句 / 语句给出的值个数对不上。
- 其余各自独立。

**结论（供"加几个码"用）**：九个点对外说的是同一件事 —— **"生成器体不支持这个形状"**，
而其中**至少一个能从合法源码走到（#5）**。所以 **一个语义码足够，不需要三个**；
反过来说，若只改文案不改码，就是把这句语言规则挂在 `InternalDiag::UnknownInternalError`（内部错误码）下面。

**已推进到：九分支表 + 唯一实测命中的最小程序 + "一个码足够"的判据。**
## 4. 更正与停手：**"先算到局部量"这条规则是假的**

按"把初始值先算到一个局部量"去改，**仍然被拒**。三次带对照的实验（循环体都以 `i + 1` 结尾，这是 `generators_loop.cml` 的形状）：

| 程序 | 初始值写法 | 读数 |
| --- | --- | --- |
| `l.cml` | `loop i = 0 while i < 3 do { yield i; i + 1 }` | **通过**，打印 `0 1 2` |
| `k.cml` | `loop i = f() while i < 3 do { … }` | **拒绝**：`InternalDiag::UnknownInternalError` … `does not build this shape`（`k.cml:7:9`） |
| `m.cml` | `var start = f()` 然后 `loop i = start while i < 3 do { … }` | **同样拒绝**（`m.cml:8:9`） |

**所以**：
1. **"先把初始值算到局部量"不是修复** —— `m` 与 `k` 一样被拒。**这条规则不能写进诊断**，写进去就是骗用户。
2. **真正的分界**是"初始值必须是**直接在循环上写出的字面量/常量表达式**"；只要它是计算出来的（调用**或**局部量），这个形状就不构建。
3. **另一处更正**：§1 的 `6376`/`6412` 其实**可达** —— 循环体不以"下一个状态值"结尾时，命中的正是它们；
   我第一次的探针（`loop i = 0 while i < 3 do { yield i }`，缺 `i + 1`）报的是同一条内部错误。
   所以"九个点里只有一个可达"这句话**撤回**：至少 `#5`（初始值）与 `#7`/`#8`（循环体结尾）都可达。

**处置**：**停手**（按判据 E）。要么实现这个形状（把初始值提升到序言），要么报一个**说实话**的诊断：
"生成器循环的初始值必须是直接写出的常量表达式；计算出来的初始值当前不支持" —— 不给不存在的绕法。
**已推进到：判死"先算到局部量"这条拟议规则，并更正可达性（至少三处可达）。**

## 14. 普通 `loop` 上这条规则还没有落地（2026-11-03 实测）

作者的口径是「与所有 `loop` 一样，体的值是下一个状态」（设计原则 3.10）。三条读数（`std::nvm` 直接跑）：

| 程序 | 输出 |
| --- | --- |
| `loop s = 0 for i in [0, 1, 2] do { sync { println(s) }; s + 1 }` | `warning[SemanticDiag::EffectNotExecuted]`：这个 `sync` 块没人用，不因此执行 |
| `loop s = 0 for i in [0, 1, 2] do { sync { println(s) } }` | `0` `0` `0`，**没有任何诊断** |
| `loop s = 0 for i in [0, 1, 2] do { println(s) }` | `0` `0` `0`，**没有任何诊断** |

⇒ 普通 `loop` 的体不以值结尾时**不报任何码**（既不是 `LoopBodyNeedsNextState`，也不是别的码），状态停在初始值、循环照常跑完。

所以 `LoopBodyNeedsNextState` **今天只在生成器路径上落地**。要让参考里那句「与所有 `loop` 一样」在两侧都成立，普通 `loop` 的降低也得报同一个码；这一条**尚未做**，记在这里。

**一个单独记下的事实（2026-11-03）**：普通 `loop` 的**体值从来没有被读过**。`visitLoopExpr` 把 `visitStmtBlock` 的返回（整块的那个 exec 节点）直接挂成 `loop` 节点的体（`builder.cpp:3438` `*loopNode << bodyValue;`），而块里最后一句 `s + 1` 只是这个 exec 链上的一个叶子。所以 `loop s = 0 for i in [0, 1, 2] do { println(s) }` 里 `s` 一次都没被改写、**连"体没给下一个状态"都没有报**。**但原因不是"体值没被读"（2026-11-03 更正）**：`stdlib/prelude.cml:858` 的 `export func println::<T>(x: T, ..rest): T` —— **`println` 返回它自己的参数**，所以 `println(s): int`，**`do { println(s) }` 是 `s` 的不动点（`s_{n+1} = s_n`），不是漏写**。**拿它当"没写出下一个状态"的例子是错的。**

**真正的靶子今天就已经会报（2026-11-03 实测，两例）**：**(a)** 体末尾是返回 `void` 的调用 —— `loop s = 0 for i in [0, 1, 2] do { emit(s) }`（`func emit(x: int): void`）⇒ `error[SemanticDiag::LoopBodyTypeMismatch]: The body of this loop gives a value of type 'void'; the state of the loop has type 'int'.`（`2:40`）；**(b)** 状态 `int`、体末尾 `string` ⇒ 同一个码，`'string'` vs `'int'`（`2:40`）。**⇒ 这条规则不缺，也不在"类型检查期"缺 —— `builder_control.cpp:3274-3288` 用 `stateType->assignableFrom(bodyType)` 已经在做，`bodyType` 就是体最后一条语句的类型**（`ArmBuild::type` 由 `setArmResult` 从末句的值节点取，`builder_control.cpp:1583-1585`，只有 GATE 才向下取一次）。

**这个判定在仓库里是有的，缺的是有人调它（2026-11-03 第二轮）**：可赋值性由 `Type::assignableFrom(Type *sourceType) const` 给出（`include/camel/core/type/base.h:275`，各类型各自重写：`bytes.h:17`、`collection.h:68`、`array.h:194`、`func.h:289`），`void` 的规则在 `func.h:385-388`。也就是说"体值能否作为下一个状态"这句话**有现成的语言判定可以依据**，`LoopBodyNeedsNextState` 今天之所以只在生成器的手工走查里落地，不是因为编译器没有这个判定，而是因为**没有任何一层拿它来问 `loop` 这件事**。要落地就在 GIR 层（`gir/builder_control.cpp:2953` 起，`gct->loopKind()` 与类型都在手上）调它，普通 `loop` 与生成器一条规则、一个检查。

**两类诊断要分开写，不能一刀切（2026-11-03，作者 17:30 与 16:30/23:30 的并置）**：① **走不到的内部控制状态** ⇒ 用「**This is a defect of the compiler, not an error of the program.**」；② **合法程序撞到的能力边界** ⇒ **不许**出现「本版本尚未支持 / 把程序报上来 / 已知限制」这类话，**要么给出语言规则的说法，要么就是缺陷**。这两条不矛盾：前者说的是"编译器自己的错"，后者说的是"别拿版本当挡箭牌"。**谁要把它们当成一件事来"统一"，先读这一句。**

**体的值确实驱动下一个状态（2026-11-03 实测三条，收尾这一条链）**：`let r = loop s = 0 for i in [0, 1, 2] do { s + i * 10 }; println(r)` ⇒ **30**（`s` 依次 0 → 0 → 10 → 30，每轮把体的值当下一个状态）；`let r = loop s = 0 for i in [0, 1, 2] do { s + i }; println(r)` ⇒ **3**；`let r = loop s = 100 for i in [0, 1, 2] do { s }; println(r)` ⇒ **100**（不动点，不是"另有来源"）。**⇒ 原则 3.10 在普通 `loop` 上今天就成立：体的值是下一个状态，且是每轮算出来的那一个。**

**`98a500e4f` 的更正（2026-11-03 实测，闸机主机）**：那次提交把自检的 liveness 上限 180 s 提到 240 s，理由写的是「the work it bounds grew fourfold」。**数据不支持这句话。** 自检驱动的用例条数 `66 → 69`（**+4.5%**），而同一个夹具的 CPU 从 2026-10-24 的 **7.3 s（不套包装）** 涨到今天的 **62.3 s（User 43.75 + Sys 18.57）**，约 **8.5 倍**；套包装后 **68.57 s**，包装本身只占约 **6.25 s（10%）**。⇒ **240 s 不是「四倍余量」，是把一个 8 倍的回归记成了工作量增长。** 口径：**liveness 上限必须以闸机主机的最慢读数为准定倍数，别的机器只作参考**（240 s 是照另一台机的 45.8 s ×4 定的，而闸机主机今天 68.57 s，只剩 3.5 倍）。

**自检 CPU 的去向（2026-11-03，闸机主机，`--jobs 1`，`lane-resid` @ `25144890a`）**：

| 项 | CPU | 占比 |
|---|---|---|
| 整轮（不套包装直接跑 `node test/tools/selftest/run.mjs`） | 62.3 s（User 43.75 + Sys 18.57） | 100% |
| 其中 153 个包装进程（被测命令本身） | 3.25 s | 5% |
| 包装器本身（68.57 − 62.32） | 6.25 s | 10% |
| `git` 通道（58×`status --porcelain` + 58×`rev-parse HEAD`） | 2.0 s | 3.2% |
| 余下 | 约 57 s | 未归属 |

**哪些数来自改动过的树**：`git` 通道那一行（116 次 / 2.0 s）是在主机侧临时加了计数行（`test/tools/run-tests.mjs`）的树上取的，测完已 `git checkout --` 复原、`git status --porcelain` 为空；**该数只用于「git 占 3.2%」这一句，不代表任何其它结论**。其余各数（`[SLOT]`、62.3 s、153 个包装进程、trivial 0.015-0.02 s、1278 条、内层 96.6%）都在干净树上取的。

**未归属的 57 s**：**运行器自身的固定工作 + 每测试起进程**（153 个包装进程：`node` 104、`camel` 45、`python3` 4）。

**这条线程到此为止：已归属 18%，其余 82% 未归属；继续追需要新的测量手段，收益低于它占的时间。** 别从头再来 —— 上面这张表就是起点。