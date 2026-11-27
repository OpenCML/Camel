# task-392 · 把 `math:abs_l` 翻译成图算子，让第 17 章的定律真的发火

**来源**：`D-1128-48` ①（2026-11-28）。**任务板上限 256 已满，本卡先落在 `internal/collab/`，起活时再建板上的卡并 `reassign`。**

## 为什么

`docs/tutorial/17_teaching_a_law` 这一章要教的是"定律发火"，但它演示的定律 `abs_of_nonnegative` **从未在这个语言上发过一次火**：

```
refuted abs_of_nonnegative: proposition equality, evidence none,
  reason the rule never fired on its own left side in 64 tries,
  the rule never fired on its own left side,
  coverage beyond translation coverage: the operator 'math:abs_l' is not translated
```

原因不是讲义写错，是 `math:abs_l` 没被翻译成图算子 ⇒ 定律的左端永远匹配不上。讲义现在的输出块已按决策者要求恢复成"发火"的形状，缺口由 pending 清单记着（`check-doc-snippets.mjs` 的 `PENDING_ALL`，key `12ee3b07b5c68a43`，cn/en 各一条，理由 `math:abs_l is not translated, so the law cannot fire`）。

## 目标

把 `math:abs_l` 翻译过去，让那章演示的定律真的发火；发火后 pending 会自己报 stale，把两条删掉，缺口闭合。

## 判据（缺一不可）

1. 第 17 章那段程序**打印出 `fired`**（贴原文；讲义输出块写的就是它）；
2. `node scripts/check-doc-snippets.mjs --default-roots` **报出那两条 pending 为 stale**（key `12ee3b07b5c68a43`，cn/en 各一条）—— 报出即可，**删 pending 由协调者另派一笔**；
3. 规则库/`math` 相关计划与 `test/plans/feat/**` 的**新增红 0**（对 `newestRedTable()` 逐名比，红名逐个贴）；
4. `docs_links` 计划不因这次改动变红（pending 两条 stale 提示除外）；
5. `[provenance] tip` + `binary built` + `numstat`，且 `BUILD_EXIT=0` 与读数**同一次**运行。

## 禁

- 不改 `check-doc-snippets.mjs`；不改 `.plan.toml` 的断言；不把讲义改成"发不起来"。
- 不为让检查变绿改语义（语义与设计原则冲突时以原则为准，记缺陷）。

## 落点（先报再动手）

规则库与算子翻译所在模块（`src/stdlib/math*` 与规则库）。**第一件事是读代码报"落点 + 打算怎么翻"给协调者**，再动手。

### 落点（2026-11-28 只读读出来的，未改一行）

**一句话：不是"算子 URI 对不上"，也不是"翻译表缺一条"就够。三处独立缺口，只有一处接在 `never fired in 64 tries` 上；而讲义那行输出块按现在的 `check` 形状根本打不出来 —— 那一处要决策者先定。**

#### 1. `math:abs_l` 在哪声明，它已经是什么

- 声明：`src/stdlib/math.cml` 的 `abs_kernel` 一族里，`#op('math:abs_l')` 挂在 `func abs_kernel::<T>(x: T) => T when is_same::<T, int>()`（同族 `abs_i`/`abs_f`/`abs_d`/`abs_bf`/`abs_f6` 逐个）。
- 公开面：同文件 `#impl(abs_kernel)` + `export func abs::<T>(x: T) => T when is_real::<T>()` —— `abs` 没有自己的体，调用直接落到内核算子上。
- 内核：`src/stdlib/math/operators.cpp` 的 `getMathOpsMap()`，`{"abs_l", __math_abs__}`。
- 算子特性：`src/stdlib/math/module.cpp` 的 `MathModule::load()` 里，`OperatorTable::instance().set("math:" + name, {.pure = true, .elementwise = false})` —— **`math:abs_l` 已经是纯算子**，所以"没人声明 ⇒ 写全局资源/可能 fault ⇒ 删保护挡住定律"这条路**不成立**（这条我先排查过，别再走）。
- 数值语义：`src/stdlib/math/semantics.cpp` 的 `declareElementaryFunctions()` 里 `math:abs_i`/`math:abs_l` 用 `exactInteger(...)`，文本说"最小整数没有正对应值，所以它的绝对值未定义"；而 `docs/stdlib-reference.md` 的 `abs` 条目说"最小整数是自己的绝对值（取负回绕，如 `-x`）"。**两处文本互相矛盾**，内核按回绕（`__math_abs__`）；这是顺带记的一条文案缺陷，不是本卡的任务。

#### 2. `never fired in 64 tries` 的真因：**与 `abs` 的翻译无关**

- 有条件（`when`）的定律**不走值比较**，只走引擎路：`src/stdlib/rewrite/module.cpp` 的 `checkOneRule()` 里，`conditional = source.when != nullptr`，值比较循环的条件是 `!conditional`，引擎路取 `instances = conditional ? 64 : 8` —— 这正是那句"64 tries"的来源（无条件定律是 8）。
- 引擎路的实例**只把规则自己声明的 `const` 形参物化成常量**：`constParams = constMaskOf(source)`，随后 `for (i < params)` 里只有 `isConst` 的端口被 `materializeStaticValue` + `replaceAllValueUses` **替换掉**；普通形参（本例的 `x`）**留在端口上**。
- 于是 `when nonnegative(x)` 是在**端口**上求值的：`fact nonnegative` 的 `match v { case const c => c >= 0; case x * x => true; case _ => false }`（`docs/tutorial/examples/17_fact.cml`）要求匹配到一个**常量**写作；端口落到 `_ => false`。64 次全不成立 ⇒ `stats.applied == 0` ⇒ 判 `refuted`，理由正是"the rule never fired on its own left side in 64 tries"。
- **推论（关键）**：把 `math:abs_l` 翻译好、覆盖列变绿，这条定律**依旧 0 次发火**。要让讲义那章真的发火，起手改的是这里：让引擎路的实例把**每个标量形参**也用抽到的样本值物化（现在只物化 `const` 形参），前提（fact）才有值可问。改动面 `module.cpp` 的 `checkOneRule()` 引擎路那一段；影响面只落在"以前条件不成立、从没发过火"的条件定律上。

#### 3. 覆盖列那半（`the operator 'math:abs_l' is not translated`）

- 翻译面只收**核心整数算子**：`src/stdlib/verify/translate.h` 的 `Term::Kind`（`Constant`/`Parameter`/`Add`/`Sub`/`Multiply`/`Divide`/`Remainder`/`Negate`，**没有 `Abs`**）、`src/stdlib/verify/translate.cpp` 的 `integerOperator()` 表（只有 `:op/add_l … :op/neg_i` 十二条）、拒绝处 `operate()` 里两处 `the operator '{}' is not translated`、能力表字符串 `coverageTable()`。
- **`math` 模块里没有任何一个算子被翻译过**（`abs_f`、`min_l`、`max_l`、`sign_l` 一样不在表里）⇒ 你问的"找一个已翻译的同类算子照抄"，答案是：**同一模块里没有**；形状最近的已翻译兄弟是核心的 `:op/neg_l`（一元整数）：声明在 `src/stdlib/prelude.cml`，翻译成 `{Kind::Negate, 1}` 一行，`render()` 里一个分支。
- 照 `:op/neg_l` 抄，四处（全在 `src/stdlib/verify/`）：
  1. `translate.h` 的 `Term::Kind` 加 `Abs`（一元，复用 `left`）；
  2. `translate.cpp` 的 `integerOperator()` 表加 `{"math:abs_l", {Kind::Abs, 1}}`（顺手加 `math:abs_i`，两宽度同形）；
  3. `translate.cpp` 的 `render()` 加 `abs(<x>)` 分支；
  4. `translate.cpp` 的 `coverageTable()` 加这一条，能力表才和代码一致（那张表是"设计 8.1 的能力表当值"，报告要出版它）。
- 注意 `operate()` 已经做了宽度与 arity 校验，`Abs` 只需 arity 1 + 结果同宽，无需 lowering 分支、无需 `minOpset` 一类字段 —— 这一层没有 lowering，只有"认不认这个 URI + 怎么渲染成项"。

#### 4. 讲义那行输出块本身打不出来（**要决策者拍，车道别自己定**）

讲义写的期望行是：
```
sampled abs_of_nonnegative: proposition equality, evidence samples 100, fired 8 of 8, coverage integer scalar int: abs(x) = x
```
但这条定律带 `when` ⇒ `conditional` 恒真 ⇒ 即使第 2、3 条全修好，`check` 打出来的也是**条件路**的形状（`checkOneRule()` 的 `else if (conditional)` 分支）：
```
sampled abs_of_nonnegative: proposition equality, evidence samples 100,
  the condition selects the instances, so no sampled value was compared,
  coverage integer scalar int: abs(x) = x, no premise yet,
  the condition selects the instances, fired N of 64
```
—— 是 `fired N of 64`，不是 `fired 8 of 8`，而且多出"条件选实例"那句和覆盖列的"no premise yet"。所以两条路选一条，得决策者定：
- (a) **改讲义那行**成条件路的形状（`pending` 的 `text` 与清单理由一起改；本卡的目标"让讲义发火"仍成立，只是发火的行长这样）；
- (b) **改 `check` 的条件路**：让条件定律也做值比较（`samples 100` + `fired 8 of 8` 才有意义）。那是 `check` 的语义改动，会动到所有条件定律的行，属 `D-` 级决定。
其中 (a) 只需改文档，(b) 要改语义；建议先按 (a) 让"发火"这件事成立，再单开一卡讨论 (b)。

#### 5. 起手验证命令（车道接手时照跑）

```
# 1) 看那条定律现在/修好后打什么（第 17 章的程序本体）
out/latest/bin/camel docs/tutorial/examples/17_fact.cml
# 2) 看两条 pending 何时自己变成 stale（判据 2）
node scripts/check-doc-snippets.mjs --default-roots --camel out/latest/bin/camel
# 3) 覆盖列单独看（--explain 或 check 的 coverage 列都走同一条 translate 路径）
out/latest/bin/camel docs/tutorial/examples/17_fact.cml
```

#### 6. 一句话给接手的人

**先改第 2 条（引擎实物化形参）——那才是"发火"的闸；第 3 条只是覆盖列的字；第 4 条的形状要等决策者。**

## 排期

**不占 CC 格**，排在 CC 之后的第一个空格（`D-1128-48` ②）。
## 接单记录（2026-11-28 07:05）

- `lane-kinds-oper`：上下文到极限，未开工（`op-names` 树零改动，已转 `D-1128-51` 给 `lane-oper-shape-guard`）。
- `lane-witness`：余量已尽，连"读卡 + 定落点"一轮都撑不完，未开工（`witness` 树 `git status` 空、`lane/item-13b @ 4128b8f8cf`）。
- **⇒ 回到排队**：`D-1128-48` ② 的原话是"不占 CC 格，排在 CC 之后的第一个空格"。现在开发位 8/8 全满、合并位 2/2 在忙、缓冲 1/1 占着，**没有空格可给**；本卡继续留在 `internal/collab/`，等某条车道自然交回再起。
- 谁接手都要先做第一件事：**报"落点 + 打算怎么翻"**（在规则库里找 `math:abs_l`，找一个同类算子照抄翻译，再改图算子表），再动手。
## 实测读数（2026-11-28 07:2x，探针格 `lane-parked-rebase`，`perf` 树）

`abs(INT_MIN)` 两个执行后端都返回 `INT_MIN`（回绕取负生效）：

```
import { abs } from math
func main() {
    let m: i32 = -2147483647 - 1
    println(m)
    println(abs(m))
}
→ [PASS] nvm  [stdout] -2147483648 / -2147483648
→ [PASS] fvm  [stdout] -2147483648 / -2147483648
→ std::cpp 产物含 `camel::standalone::intAbs`（文本级；运行级要仓库内 case + cppexec 的 expect.txt）
```

**坑（写用例的人必须照抄）**：直接写 `abs(-2147483648)` **测不到 Int32 分支** —— 字面量超出 `i32`，整个表达式按更宽类型算，实测两个后端都打 `2147483648`。要测 Int32 的最小值必须**显式声明宽度**：`let m: i32 = -2147483647 - 1`。
