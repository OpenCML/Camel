# 行注释写在表达式中间时，格式化把注释后面的代码并进注释

（由 `handover/h403-parsecheck-crash` 提出。同一轮 `camel-parsecheck fuzz` 里的**两处 SIGSEGV** 已在本车道修好
并落地（命名类型参数缺名、注释区间的反向查找越界）；这是同一轮里**另一类**失败，按父代理 2026-10-24 的分工
单独交回，本车道不修。）

## 问题

不算疑问的那半：格式化器写出一个能解析的文本，是 `test/plans/feat/parse/frontend.plan.toml` 第 33 行
（`frontend.format_keeps_ast`）已经写下的规则——"where the formatter breaks a line or removes a space, the two
layout rules must not change the meaning"。下面这条把它破了。

`Formatter::insertComment`（`tools/format/fmt.cpp:120-144`）把注释原样写出后**只对块注释**（channel 4）做
"同一行还有代码就补一个空格"的处理；行注释（`//`，channel 1/2/3）写出后**不结束这一行**，于是紧跟其后的
代码被注释掉，格式化后的文本不再解析。

最小复现（输入 ACCEPT，输出 REJECT）：

```camel
func main(): int {
    let x = 1
    let y = 2
    let eq = if x == y // the two are equal here
    then 1 else 0
    return eq
}
```

- `camel-format <该文件>` 写出 `    let eq = if x == y // the two are equal here then 1 else 0`；
- 把写出的文本读回：`SyntaxDiag::MissingThen`（`0x02000003`），位置在第 4 行；
- `camel-parsecheck fmtcheck <只有这个文件的列表>` 自己就报
  `CHANGED <file> (does not parse) ... changed=1 unstable=0`，退出码 1。

需要裁定的只有**写在哪里断行**（不是"要不要修"）。

## 数字

`camel-parsecheck fuzz <仓库 2648 个 .cml 的列表> 3000 <seed>`，seeds 1..300，每颗种子只记第一处失败
（主机 32 核，2026-10-24）：

- 修那两处崩溃**之前**：7/300 颗种子 SIGSEGV（已修），其余失败与下面同一批。
- 修**之后**：0 崩溃；**60/300** 颗种子仍失败——其中 **53** 颗是本条（`the formatted text does not parse`），
  另 **7** 颗是 `the leaves of the tree are not the tokens of the text`（CST 对畸形输入不无损，另一类，见下）。

## 选项与代价

- **选项 A：行注释写出后一律 `lineEnd()`**。代价：所有"行注释后面同一行还有代码"的地方都多一个换行，
  相关 golden 要重录；要顺带确认"再格式化一遍不变"（`unstable=0`）仍成立。
- **选项 B：只在注释后面同一行确实还有代码时才断行**（判据与块注释现有的
  `tokens[index + 1].line() == comment.line() + countLines(commentText)` 对称）。代价：判断多一处边界，
  golden 动得比 A 少，且与块注释的处理成对。

## 建议

判 B（与块注释的现有判断对称、golden 动得最少）。无论判 A/B，验收都应包含：把上面 5 行写成
`test/cases/parse/**` 或 `test/cases/format/**` 里的一条固定最小输入，并在
`frontend.format_keeps_ast`（`fmtcheck`，`changed` 是它的门）或一条 `camel-format` 的 snapshot 里看它从红转绿。

## 另有一类（同轮扫描的残留，未展开）

7/300 颗种子报 `the leaves of the tree are not the tokens of the text`：输入的 CST 与词的流不一致，只有
fuzz 的这个不变量看得到（`dump` 是 REJECT、`fmtcheck` 直接跳过，两者都不报）。最小形态是文件在赋值中途
结束，例如 `resource a =\n`（截断到 EOF）。修前它被崩溃盖住，修后才露出，属另一条车道。

## 出处

- 规则：`test/plans/feat/parse/frontend.plan.toml:33`（`frontend.format_keeps_ast` 的 `desc`）。
- 工具自检：`tools/parsecheck/snapshot.cpp:114` `runFormatCheck`（`changed`/`broken`/`dropped` 是它的门）。
- 位置：`tools/format/fmt.cpp:120-144` `Formatter::insertComment`，调用点在 `tools/format/fmt.h` 的
  `formatList`（`insertComment(j, result)`，两处：列表首元素前的注释、每个元素后的注释）。
- 数字来源：本车道 2026-10-24 的 seeds 1..300 扫描（日志在 `D:\Projects\Camel-wt\notes\h403-verify2.log`
  与 `h403-sweep-*.log`，工作树之外）。