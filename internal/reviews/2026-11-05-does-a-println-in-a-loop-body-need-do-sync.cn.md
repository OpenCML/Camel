# `loop` 体里的 `println` 不产出输出：要采纳的写法是 `do sync`，而 `EffectNotExecuted` 的 help 指错了补救？

2026-11-05，`lane-obey`，读数取 `ab84671c1`。二进制是本机安装 `Camel v0.2.42
(Build 261102_150415_v0.2.41_1010_ge5184edda_dirty)`（`D:\Projects\Camel\out\latest`，本机不构建，所以它是
11-02 的安装；下面两个行为在源码与语言参考里都写着，不依赖某个构建）。

这一页先纠正派工的一个前提。派工把这件事记成"`loop` 体里的 `println` 不进输出 = **缺陷**"。读数的答案更窄：
**行为本身是语言规则，缺陷在诊断给的那句补救上**——按规则写，输出正常；按诊断的 help 写，仍然没有输出。
所以下面既不说"本版本尚未支持"，也不说"程序写错了"：程序没有错，它只是没写 `do sync`，而**诊断告诉它去写的
那个写法，在 `loop` 体里不成立**。

## 1. 问句

**`loop` 体里的 `println` 不产出输出：这一格要采纳的写法是不是 `do sync { ... }`，而
`SemanticDiag::EffectNotExecuted` 的 help 该不该补上这个写法（它现在给的两条补救在 `loop` 体里都不成立）？**

- **规则一侧**：`docs/language/06_expressions.md` 的 6.8.6 效应那条原话——「循环体是不是同步块由它自己决定
  （3.2）。写成 `do sync { println(x); acc + x }` 时每轮按顺序执行其中的效应。」同一节还说 `do` 之后是一个
  表达式，`sync { ... }` 是块。设计原则 3.1/3.2：求值由依赖驱动，`sync` 块本身没人用就不执行；3.4：同步块
  不向内继承，臂、循环体与嵌套块按它们自己的写法执行。
- **缺陷一侧**：诊断 `SemanticDiag::EffectNotExecuted` 的 help 原文——「To run it, put it on a sync chain
  ('sync { ... }' in a function body, or use the block's value), or use its result.」在 `loop` 体里，第一条
  说的位置不是函数体（写成语句的 `sync { ... }` 按 3.2 整个不执行），第二条 `let u = sync { ... }` 是**语法
  错误**。两条都不通，而按 6.8.6 唯一能通的那个写法（把体写成 `do sync`）**help 里一个字也没提**。

## 2. 读数（最小复现与四个结果）

复现程序（五行，`test/cases` 里没有这一条，本页即它的记录）：

```
func main(): int sync {
    let total = loop i = 0 while i < 3 do {
        'i={}'->format<i>->println
        i + 1
    }
    'total={}'->format<total>->println
    return 0
}
```

| 写法 | stdout | 诊断 |
| --- | --- | --- |
| ① 体里直接写 `println`（上面这段） | `total=3` | `warning[SemanticDiag::EffectNotExecuted]: The call of 'println' has side effects but is never executed: nothing depends on it and it is not in a sync block.` |
| ② 体写成 `do sync { ... }` | `i=0` / `i=1` / `i=2` / `total=3` | 无 |
| ③ 体里加一个 `sync { println }` 语句（照 help 的第一条） | `total=3` | `EffectNotExecuted`，措辞换成 `the sync block it is in is not used` |
| ④ `let u = sync { ... }`（照 help 的第二条） | 无输出 | `error[SyntaxDiag::UnexpectedToken]: Unexpected 'sync'; expected an expression.`，exit 1 |

- ① 与 ② 只差一个词：`do sync`。② 的状态不变、轮数不变、循环的值不变（`total=3`），**它就是把体声明成
  同步块**——6.8.6 与 3.4 说的"在那里写 `sync`"就是这件事，而 `docs/language/06_expressions.md` 的原话已经用了
  这个写法。
- ② 换成 `for` 取元素也一样（`loop acc = 0.0 for x in xs do sync { println(x); acc + x }` 打印三行
  `x=1.0`/`x=2.0`/`x=3.0`），所以这不是 `while` 一侧的问题。
- ③ 是最该注意的一格：**help 让人写的那个东西写出来了，警告换了句、输出还是没有**。`docs/design-principles.cn.md`
  3.2 那句「作为语句写在非同步的函数体里、值又没人用的 `sync { ... }` 整个不执行」正是原因——**help 把"写一个
  同步块"与"这个块会被需要"混成了一句**。

## 3. 三条路，各自要改什么、改多大

- **① 采纳 `do sync { ... }` 作为写法，并把 help 补上它（默认）。** *谁要改*：写法这一半不用改任何东西，仓库里
  已有大量先例（`test/cases/compile/stream/generators_body_fault.cml`、`examples/models/naive-in-expert-out/accuracy.cml`
  都是 `loop ... do sync { ... }`）；help 那一半在 C++（`SemanticDiag::EffectNotExecuted` 的 help 文本），本车道
  不改（本轮只许改一处代码）。*代价*：不改 help 的话，**每个照它做的人都要先撞一次 ③**，然后自己回去读 6.8.6。
- **② 让 `loop` 体里的 `sync { ... }` 自动成为同步链（改语义）。** *谁要改*：作者。*改多大*：3.4 那句"同步块不
  向内继承"的直接例外，等于改规则。*代价*：同一个块的含义会随它写在哪里而变——正是 3.4 用一句话禁掉的事。
- **③ 继续把曲线折成数组、循环外打印。** *谁要改*：不需要谁点头，`test/cases/showcase_s2/train_curve.cml` 已经
  这么做（**该文件在 `lane-lgrad` 上，不在 `develop`**：`48e0327c7`）。*代价*：输出少 12 行曲线，而这 12 行是
  一个训练用例最该让人看见的东西；它没有错，只是**在 `do sync` 之前写的**——按 ①，它可以改成每轮打印，**但那是
  那条车道的事，本任务不动它**。

## 4. 默认（我方按此继续，不等回复）

**①**：写法上是 `do sync { ... }`——**这是要采纳的写法，不是"未实现"**；`SemanticDiag::EffectNotExecuted` 的
help 在 `loop` 体里指错了补救——**这是一条要修的缺陷，不是"程序写错了"，也不是"本版本尚未支持"**。

本轮的动作只有两件：把这一页写下来（复现与四个结果在上面）；**不改** help（本轮只许改 `stdlib/vjp.cml#with_grad`
那一处），**不改**任何用例的期望值，**不动** `test/cases/showcase_s2/train_curve.cml`。

留给下一轮的判据（谁修 help，按这个验收）：

- 复现程序的 ① 与 ③ 仍各给一条 `EffectNotExecuted`（**行为不因 help 的改动而变**），② 仍打印 `i=0`/`i=1`/`i=2`；
- 改后的 help 里必须出现 `do sync` 这个写法（`git grep -n "do sync" -- src` 至少命中这一处），且不再把
  "'sync { ... }' in a function body" 当成唯一出路。