# `time_sensitive` 的两个读者

2026-11-25。`time_sensitive` 决定一次运行要不要独占整台主机，而它只有一个读者：闸门
（`internal/tools/camel-remote.ps1`）。一个东西只有一个读者时，读者错了没人能发现——所以这一页接着
`internal/plans/2026-11-24-daily-review.cn.md` 第 8 条，把那条读数变成两个实现之间的一次比对。

本页只记读数与判定；判据本身（为什么这样切、为什么这样比）写在两处代码的头部：闸门那半在
`internal/tools/camel-remote-timesensitive.ps1`，第二个解析器在 `test/tools/time-sensitive-reader.mjs`，
比对在 `internal/tools/camel-remote-timesensitive.test.ps1`。

读数前提（四样）：群体＝本树全部 `*.plan.toml`（480 个，排除 `.git`／`node_modules`／`out`）；
时刻＝2026-11-25 15:2x；来源＝工作树 `D:\Projects\Camel-wt\time-sensitive`，分支 `lane/time-sensitive`，
基于 `git fetch` 后的 `origin/integration` `d3e0cb39ae`；前提＝本机 PowerShell 7 + node，
**未构建、未跑任何计划**（`.test.ps1` 这一类的约定：不需要主机、不需要构建）。

## ① 第一个解析器：每一句话对应哪一种输入

这一处就是今天唯一决定「要不要独占主机」的代码，共 11 句。改造前在原位
（`camel-remote.ps1:1884-1899`），现在整段搬进 `internal/tools/camel-remote-timesensitive.ps1`
的 `Get-TimeSensitivePlans`，**逐句未改**（搬迁见 ②）。

| # | 那一句话 | 它决定的是哪一种输入 |
|---|---|---|
| 1 | `foreach ($p in $Plan)` | `-Plan` 分出来的每个目标（逗号切分在 `camel-remote-args.ps1` 里已经做过） |
| 2 | `$p = "$p".Trim()` | 前后带空白的目标（`' feat/x.plan.toml '`） |
| 3 | `if (-not $p) { continue }` | 空目标：`,`、`''`、纯空白 |
| 4 | `IsPathRooted($p)`，否则 `Join-Path $Root $p` | 绝对路径目标 vs 相对树根的目标 |
| 5 | `Test-Path -LiteralPath … -PathType Leaf`，否则 `continue` | 目录目标（`smoke`、`feat/opt`）、不存在的目标、名字里含 `*`／`?` 的目标（`-LiteralPath` 不展开通配符） |
| 6 | `Get-Content -LiteralPath` 逐行 | 文件内容本身：行尾（CRLF/LF）、编码、BOM；`Get-Content` 会去掉行尾的 `\r` |
| 7 | `if ($declLine -match '^\s*\[') { break }` | 第一个「首个非空白字符是 `[`」的行。它想说的是「表头」，但它不区分：`[defaults]`、`[[tests]]` 是表头；多行字符串里的一行 `[OO] …`、多行数组里的嵌套元素 `[1, 2],` 不是表头，它同样在那里停下 |
| 8 | `-match '^\s*time_sensitive\s*=\s*true\s*(#.*)?$'` | `time_sensitive` 的写法：`=` 两边空格可有可无、值必须是裸词 `true`、可跟注释。**大小写不敏感**（PowerShell `-match` 的默认），所以 `True` 也命中；带引号的 `"true"`、`false`、其他值不命中 |
| 9 | `$declared += $p; break` | 一个文件里第一处命中即停：同一文件写了两次时，第二处不再看 |
| 10 | `$timeSensitivePlans.Count -gt 0` | 多个 `-Plan` 之间取「或」：任一目标声明了，整次运行就独占主机 |
| 11 | bash 侧 `if [ '$timeSensitive' = 'True' ]` | PowerShell 的 `$true`／`$false` 插值成 `True`／`False` 再与字符串比较：这一句和上一句之间靠**大小写**对上 |

第 7、8、11 句是全部风险的所在：7 与 8 是「读法」，11 是「读到的值怎么传过语言边界」。

## ② 第二个解析器在哪，凭什么算独立

`test/tools/time-sensitive-reader.mjs`。它不 import 本树任何东西——不 import 闸门那半，不 import
跑测工具的 `test/tools/toml-lite.mjs`，也不 import 跑测工具。共用的只有输入：计划文件里的字符串。

写法上两者没有一处同形：

| | 第一个解析器 | 第二个解析器 |
|---|---|---|
| 语言／机制 | PowerShell，**逐行两条正则** | JavaScript，**逐字符扫描** |
| 「表头」怎么认 | 行首非空白是 `[` 就停 | 只有「行首 + 没有值正在写（`[`/`{`/`"` 的深度为 0）」才是表头 |
| 字符串 | 不知道有字符串 | `"`／`'`／`"""`／`'''` 全部吃掉，字符串里的 `[`、`#`、`=` 都不是结构 |
| 值 | 正则里写死裸词 `true`，且大小写不敏感 | 按 TOML 只认小写 `true`／`false`，其余记 `invalid` |
| 缩进无关 | 只认顶层的行 | 只认顶层（表头之后一律归那一张表），并另外报「写在表里」 |
| 能报错吗 | 不能：只有 true／false 两个答案 | 能：`state` 四种，外加 `problem` 说明跑测工具会因何拒收该文件 |

「没复用」的可核对读数（比对脚本自己每次都在查，见 ③ 的 wiring 三条）：`camel-remote.ps1` 里
`Get-TimeSensitivePlans` 出现 1 次、声明正则出现 **0** 次；`camel-remote-timesensitive.ps1` 里那条正则
出现 **1** 次；`time-sensitive-reader.mjs` 里 `import` 行只有 `node:` 内置模块。

## ③ 全树交叉核对读数

| 读数 | 值 |
|---|---|
| 树里的 `*.plan.toml`（排除 `.git`／`node_modules`／`out`） | **480**（`test/plans` 430、`test/tools` 49、`internal/reports` 1） |
| 两侧拿到的文件集 | 同一个：480 = 480（比对脚本断言这一条，因为「量的那棵不是要改的那棵」会让读数失效） |
| 闸门读成 `time_sensitive = true` 的文件 | **1**：`test/plans/perf/fib.plan.toml`（第 25 行） |
| 第二个解析器读成 `gate = true` 的文件 | **1**：同一个 |
| 逐条不一致 | **0** |
| 第二个解析器的顶层键名与表名 vs 跑测工具 `toml-lite.mjs` 的顶层键名 | **480/480 相同** |
| 跑测工具能读的文件里，两者对 `time_sensitive` 的答案不同 | **0**（480 个全部能读） |

即：**今天这棵树里没有一处不一致**。这句话只有配上 ④ 才有意义——两个读者对「今天树里那一处」都答对，
不等于它们读得对。

比对脚本自己报 **27 条检查**：4 条 wiring（闸门调了这个函数、闸门里没有第二份正则、这份正则只有一处、
闸门确实 dot-source 了它）、5 条目标种类（相对、绝对、目录、空、不存在——第 ①表第 1-5 句）、
3 条总体（两侧文件集相同、逐个文件答案相同、声明的那一个两边都找到）、15 条边界形状。

一条顺带量到的、树里已经存在的形状：`test/plans/feat/opt/operator_records.plan.toml` 的顶层
`desc = """…"""` 里有以 `[` 开头的行（第 16 行 `[OO] oper=N …`），而它真正的第一个表头在第 40 行。
也就是说第一个解析器在这棵树上**已经**有一个「提前收表」的文件——只是那个文件没有声明
`time_sensitive`，也没有人在它下面加过这一行，所以没人看见。

## ④ 边界输入：四种 + 另外三种

同一批文件，两侧都读一遍（`internal/tools/camel-remote-timesensitive.test.ps1 -Keep` 可留档）。
「跑测工具」一列来自 `test/tools/toml-lite.mjs` + `validatePlanSchema`，用来判谁对。

| 形状 | A 闸门 | B 第二个 | 跑测工具 | A 与 B |
|---|---|---|---|---|
| 空值 `time_sensitive =` | false | false（`invalid`） | 拒收（`unsupported TOML value: `） | 相同 |
| 带引号 `time_sensitive = "true"` | false | false（`invalid`） | 解析成字符串 → 类型检查拒收 | 相同 |
| 大小写混写 `time_sensitive = True` | **true** | false（`invalid`） | 拒收（`unsupported TOML value: True`） | **不同** |
| 前后空白 `   time_sensitive   =   true   ` | true | true | 接受，`true` | 相同 |
| 重复标记（同一键两次） | true | true（并报重复） | 拒收（`duplicate key`） | 相同 |
| 声明在顶层多行字符串之后（字符串里有一行以 `[` 开头） | **false** | **true** | **接受，读到 `true`** | **不同** |
| 声明写在 `[defaults]` 里 | false | false（报「属于那张表」） | 读不到顶层键；`[defaults]` 无此字段 → 拒收 | 相同 |
| 带引号的键 `"time_sensitive" = true` | **false** | **true** | 拒收（`unsupported TOML line`） | **不同** |
| 键写在字符串**里面**（是散文） | **true** | false（`absent`） | 接受，读不到顶层键 | **不同** |
| 声明在多行嵌套数组之后 | **false** | **true** | 拒收（多行数组不支持） | **不同** |
| 对照：裸 `time_sensitive = true` | true | true | 接受，`true` | 相同 |
| 对照：`time_sensitive = false` | false | false | 接受，`false` | 相同 |
| 对照：没有这个键 | false | false | 接受，无此键 | 相同 |
| 对照：值后带注释 | true | true | 接受，`true` | 相同 |
| 对照：`time_sensitive=true`（无空格） | true | true | 接受，`true` | 相同 |

「差异必须有一侧报错」的口径下：B 在每一处不同里都给出了 `state`／`problem`（它报得出来），
**A 一处也报不出来**——它的整个接口只有 true／false。所以 ④ 要求的「若两侧都接受但答案不同那就是必须修的
不一致」，在这五项上成立，其中一项已经到了危险的一侧（见 ⑤）。

## ⑤ 五处不一致，逐条判谁对

1. **`True`（大小写混写）**：A 读成 true，B 与 TOML 都说这不是布尔。**A 错**：PowerShell 的 `-match`
   默认不区分大小写。后果是多占一次主机（跑测工具会拒收这个文件，所以不会假绿）。
2. **声明在顶层多行字符串之后**（`desc = """` 里有一行 `[note]`，其下 `time_sensitive = true`）：
   A 读成 false，B 读成 true，**跑测工具接受这个文件并读到 `true`**。**A 错，而且错在危险的一侧**：
   这就是那个字段存在的理由被绕过——一个 TOML 合法、跑测工具照跑的计时计划，会在别的构建旁边计时，
   而闸门以为它不是时间敏感的。这是本次唯一一条**可达的假绿**。
3. **带引号的键**：TOML 允许 `"time_sensitive" = true`，A 漏。按格式 **B 对**；但跑测工具读不了这种
   写法（`unsupported TOML line`）会拒收该计划，所以今天不会因此假绿。
4. **键写在字符串里面**：A 读成 true（把散文当声明），B 说没有声明。**A 错**，后果同样只是多占主机。
5. **声明在多行嵌套数组之后**（`ids = [ … [1, 2], … ]`）：A 在第 2 行就收表，漏了声明。按格式 **B 对**；
   跑测工具不支持多行数组而拒收，所以今天不会假绿。

一句话：**A 在两处「多读」（1、4，代价是多占主机），在两处「漏读」（3、5，今天靠跑测工具拒收兜住），
在一处「漏读且没人兜」（2）。**

## ⑥ 没做的事，以及为什么停在这里

**没有改第一个解析器的行为。** 派工写明：交叉核对证明它错就停手报。第 ⑤ 条第 2 项正好证明它错，
所以这里只把证据摆出来，不顺手修——修法有三种，每一种都要先裁定：

- 把第 7 句的「收表」条件换成真表头（`^\s*\[\[?[^\]]+\]\]?\s*(#.*)?$`）：修 2 与 5，1、3、4 仍在；
- 闸门改问跑测工具（`2026-11-24-daily-review.cn.md` 第 8 条原本的修法）：需要闸门侧多一个 node 依赖，
  且与「跑测工具只量、闸门决定在哪量」的分工要重新讲一遍；
- 闸门改调 `test/tools/time-sensitive-reader.mjs`：等于把 A 降级成 B 的包装，A 就不必存在了。
  第 ② 节说 A 逐句未动，正是为了这个裁定还没有之前，读数仍然可比。

另有一条不属于本道、但必须留话的：跑测工具的 `toml-lite.mjs` 不认带引号的键，也不支持多行数组。
这两个不是本道要修的缺陷（它拒收是响的，不是静默的），但它是第 ⑤ 条里 3、5 两项「今天为什么没出事」
的唯一原因——一旦 `toml-lite.mjs` 学会这两种写法，3、5 就变成和 2 同类的缺口。

## 怎么重跑

```powershell
# 两个读者 + 15 个边界形状（本机，无主机、无构建、无计划）
pwsh -NoProfile -File internal/tools/camel-remote-timesensitive.test.ps1

# 第二个读者自己的判据（本机；同一个文件也是 feat/tools/selfchecks 的一个用例）
node test/tools/time-sensitive-reader.test.mjs

# 只要第二个读者的读数
node test/tools/time-sensitive-reader.mjs test/plans/perf/fib.plan.toml
```