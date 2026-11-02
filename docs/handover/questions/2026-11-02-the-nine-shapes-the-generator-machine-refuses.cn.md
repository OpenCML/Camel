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

**一个单独记下的事实（2026-11-03）**：普通 `loop` 的**体值从来没有被读过**。`visitLoopExpr` 把 `visitStmtBlock` 的返回（整块的那个 exec 节点）直接挂成 `loop` 节点的体（`builder.cpp:3438` `*loopNode << bodyValue;`），而块里最后一句 `s + 1` 只是这个 exec 链上的一个叶子。所以 `loop s = 0 for i in [0, 1, 2] do { println(s) }` 里 `s` 一次都没被改写、**连"体没给下一个状态"都没有报**：规则缺的不是一句诊断，而是**"体的值"这个位置本身**。要判"体值类型 vs 状态类型"，得在**类型检查期**做，降低期拿不到类型。