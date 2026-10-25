# `fact` 落地后要裁定的五件事：`with` 实参的读法、通用头、`join or`、第 17 章的文本块、没改到的字面

本次交付把 `fact` 与 `fact` 的 `with` 实参落了地（语法、AST、引擎、诊断、格式化器、文档块与用例改写）。
下面五处需要作者裁定或落笔；每处先给今天的读数，再写要什么。

## 一、`with` 实参能被查询写出、能参与键，但**还不能被 case 读**

今天的读数（`with <cap: int = 5> fact capped(v: int): int match v { case const c => c case _ => 0 } join max`，
两条规则分别用 `capped(x)` 与 `capped<1>(x)` 读它，跑一次改写遍）：

```
product 1
product 0
3
```

两条规则都开火了（`product` 的乘法被改写成左操作数，`main` 打印的 `3` 就是改写后的 `product(3, 4)`），
即：**查询写出实参、省略实参（取默认值）两条路都在跑**；引擎按"事实 + 实参"建了两个 `Definition`
（键 `capped` 与 `capped<1>`、事实种类 `user.fact.capped` 与 `user.fact.capped<1>`、两个分开存的值 ✓
`modules/rewrite/analysis.cpp` 的 `analysisKernel`）。

但实参**只能进键，不能进值**：case 与 `join`/`meet`/`widen`/`narrow` 都由引擎用"值本身"调用，
拿不到查询写下的实参。今天写出来是这样被拒的（`SemanticDiag::ConstructNotSupported`）：

```
'fact' is not supported: a clause cannot name a 'with' parameter of the fact: the engine calls the cases
with the value the fact is about, not with the argument the query was written with, so the argument is
only part of which fact this is.
```

**要把这一条做完，需要一处引擎接口的改动**（两条路，任选）：

1. `modules/rewrite/analysis.cpp` 的 `Frame::call` 把 `Definition` 里存下的实参（内核实参第 7 个起的
   `slot_t`，对象型的值要用 `mm::RootHandle` 挂住）交给 `NodeVMSchedPass::callGraph` 的 `withArguments`
   形参；生成出来的 case/`initial`/`join` 等函数各自声明这些 `with` 参数，由引擎每次调用时给值。
2. 或者由 `src/parse/ast/builder.cpp` 在查询函数里把每个被交给引擎的函数包成**闭包**
   （`(a) => capped__case0_fact(cap, a)`），实参由闭包捕获；这一条还要 `Frame::call` 把
   `fn->tuple()` 作为 `callGraph` 的闭包形参传下去（`callWindow`/`callWhen` 那条路已经有了）。
   注意 case 的 **pattern** 函数不能这样包：`analysis.cpp` 的 `compilePattern` 直接读那张图的形状。

**请裁定**：这条是本次必须补完（我按第 1 或第 2 条另开一小段工作），还是先按今天的边界收口、
把"实参进值"留给 8.1/8.6 那条线？今天的边界已经写进 `docs/technical/46_rules.md`。
（另一条较弱的边界：`fact` 的 case 里读实参会被拒；这一点在验收 ③ 上表现为"两个实参各自的键与缓存分开，
但值相同"——值相同就没有行为差异可读。）

## 二、设计稿 7.5 的通用头（`fact range::<T>(v: T): Interval`）今天被拒

```
'fact' is not supported: a fact whose value is one of its own type parameters is not compiled: the engine is
given the functions the fact is made of, and a function with type parameters is not a value it can hold
```

原因与第七条同源：`__fact` 拿到的是**函数值**，泛型函数不是一个值（`analysis.cpp` 直接从
`::Function` 取图）。非通用的形态今天都通了：单值、多值（`fact aligned(a: T, b: U): bool` 的两参形态）
与带实参的形态。**请裁定**：通用头是目标（那需要"类型实参 → 函数实例"的新入口），还是先按多值 + 实参覆盖？

## 三、`join or` 里的 `or` 全树不存在

设计稿 8.6（`fact dominates(a: T, b: U): bool join or`）与第 17 章正文都写 `join or`，但全树没有布尔二元的
`or`：`git grep "func or("` 是 0 命中，`docs/stdlib-reference.md` 里也没有；布尔二元只有 `||` 与
`math::max/min` 的 `bool` 重载（`#op<'math:max_b'>`，false < true）。7.5 对 `join` 给的是"一个二元函数
（已有函数的名字，或 lambda）"，本次第 17 章三处（`17_rule.cml`、`17_fact.cml` 与两页正文的 5 个 fence）
都改成了 `} join (lhs: bool, rhs: bool) => lhs || rhs`。**请裁定**：`or`（与 `and`）是否要作为标准库的
布尔二元函数？

## 四、第 17 章的文本块与待实现名单的键

`fact` 落地后第 17 章的 12 个片段**全部编译通过**；仍在前面的是两处**文本块**，都是 8.1「`fact` 的结论
作为前提」的读数（`.cn.md:13`/`.en.md:15` 与 `.cn.md:316`/`.en.md:326`，原文见交付报告）。这两条名单条目
不能删（删了闸门就红），也不能照原样留：同一份程序在一页里出现四次，第 1 段有 `text` 块要比较，后三段是
`graph-dump`（只编译、不比文本），四个片段共用一个键 `e9c8cd7fea9b9890`。按原来"某个片段已能编译 ⇒ 这条
名单过期"的判据，后三段会让条目立刻过期，而第 1 段的文本块其实还在前面。本次把判据改成"**这个键下的每个
片段都追上了**才算过期"（`scripts/check-doc-snippets.mjs` 里那段注释写的就是这个理由）。**请裁定**：
这条判据对不对？若不对，第 17 章的登记要换个切法（`std-opt-and-module-rules.md` Q4 提过同一件事：
让图转储的 `text` 块参与比较）。

## 五、没改到的 `analysis` 字面（不在本次的笔的范围内）

1. `docs/design/rewrite/README.cn.md`（142、154、158、170、174、207）：v1 设计稿，7.5 取代了它。
2. `docs/design/graph-store/README.cn.md:131`。
3. `docs/design/rewrite/plan-verification.cn.md`（112、175、341）：112 行引用
   `modules/rewrite/module.cpp#analysisKernel`，`check-doc-links --citations` 会核对这个符号，
   所以引擎内核的函数名与文件名**保留旧名**（`modules/rewrite/analysis.cpp|h`）；对用户可见的一切
   （关键字、保留组 `__fact`、URI `rewrite:fact`、事实种类 `user.fact.<名字><实参>`、诊断、文档）都已改名。
4. `docs/language/08_top_level_and_grammar.md:92` 与 `en/` 同段：手写的记号说明仍写
   `analysisDecl`/`analysisClause` 与 `analysis`/`initial`/`join`/`widen`/`on`。产生式块由
   `node scripts/check-grammar.mjs --write` 重新生成（已是 `factDecl`/`factBody`/`factCase`/
   `factPattern`/`factClause`），这句记号说明不是生成的，而 `docs/language/**` 的正文不在本次的笔的范围内。

**请落笔** 1、2、4；第 3 处若要连内核符号一起改名，需要一并改那句引用。