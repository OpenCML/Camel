# `time_sensitive`：一个读法，闸门去问它

2026-11-25。这一页记的是 `internal/plans/2026-11-24-daily-review.cn.md` 第 ⑧ 条落地后的样子：**闸门不解析
TOML**，它问跑测工具；`time_sensitive` 这个字段在闸门这一侧没有第二个读法。

## 为什么是「问」，不是「对账」

第 ⑧ 条要的是「闸门问跑测工具（**同一个 TOML 解析器**），不自己解析」。先落进树里的那一版做的是另一件事：
把闸门里那两段正则搬出去、另写一个解析器、让两者互相核对。那在「这个字段只有一个读者」的前提下是**对的
做法**——一个读者错了没人能发现——但它没有消灭第二份真相，它给第二份真相配了第二个真相，而第 ⑧ 条要消灭的
正是那个。裁定 D-1125-21 把那一版撤了，这一页记的是撤掉之后的样子。

## 现在谁读这个字段

**一个**：`test/tools/toml-lite.mjs`。它不是为此新写的——`test/tools/run-tests.mjs` 加载每一个计划用的就是它
（`parseTomlFile`），`validatePlanSchema` 里对这个字段的 `boolean` 类型检查也走同一条读法。所以「闸门读到的」
与「跑测工具会照着跑的那个」是同一句话的两个时刻，不是两个句子。

只读入口是跑测工具自己的一个子命令：

```powershell
node test/tools/run-tests.mjs --print-time-sensitive test/plans/perf/fib.plan.toml
```

一行一计划，`<path>` TAB `<token>`，没有别的输出。它在 `main` 里 `--list-cases` 的同一处返回：**不装二进制、
不建、不取锁、不写盘、不启动任何计划**，因此闸门可以在决定要不要独占主机之前就问。

| token | 意思 | 闸门怎么用 |
|---|---|---|
| `true` | 顶层 `time_sensitive` 是布尔 `true` | 这一次运行独占主机 |
| `false` | 顶层是布尔 `false` | 不独占 |
| `absent` | 顶层没有这个键 | 不独占 |
| `invalid` | 有这个键但不是布尔（例如 `"true"`） | 不独占；跑测工具自己会拒收这个计划，红在它那里，不红在这里 |
| `unreadable` | 文件读不出或解析不了 | 入口退出码 2，闸门**拒跑**（见下） |

闸门这一侧只剩路径问题：`-Plan` 的目标 Trim、绝对路径照用、相对路径按树根解析、只认文件（目录目标 `smoke`、
`feat/opt` 不贡献声明）、空目标跳过。这些是「哪些目标是计划文件」，不是「文件里的格式怎么读」。

**读不到不是 `false`。** 如果这一侧没有 `node`，或者入口的退出码非 0（有文件读不出），闸门打印原因并 `exit 2`，
不开始这一次运行——一个没人做过的读数不是「不独占」。这与树探针那条规则是同一条：读不到的探针不是干净的树。

## 读数

`test/plans/perf/fib.plan.toml` 是树里唯一声明了自己的计划（第 25 行、第一个表头之前）：

```
$ node test/tools/run-tests.mjs --print-time-sensitive test/plans/perf/fib.plan.toml
test/plans/perf/fib.plan.toml	true
```

八个形状，同一个命令，同一台机器（工作树 `lane/time-sensitive`，2026-11-25）：

| 形状 | 读数 |
|---|---|
| 裸 `time_sensitive = true`（对照） | `true` |
| `time_sensitive = false`（对照） | `false` |
| 没有这个键（对照） | `absent` |
| 带引号 `time_sensitive = "true"` | `invalid` |
| 大小写混写 `time_sensitive = True` | `unreadable`（`unsupported TOML value: True`） |
| 同一个键写两次 | `unreadable`（`duplicate key 'time_sensitive' (line 2)`） |
| 声明在顶层 `desc = """` 之后，而字符串里有一行以 `[` 开头 | `true` |
| 声明写在 `[defaults]` 里 | `absent` |

最后两行值得单独看：它们正是先前两个读法会给出**不同**答案的两个形状（闸门读成 `false`／第二版读成 `true`）。
现在只有一个答案，而且就是跑测工具接下来会照着跑的那个答案——顶层 `desc` 里的 `[` 不再能让任何读者提前收表，
因为它读的是文档，不是行。

## `internal/tools` 里还剩哪些提及

```
$ git grep -n 'time_sensitive' -- internal/tools
internal/tools/README.md:390:  for a `time_sensitive = true` plan, the whole host), `buildJobs` (the build, from the build slot),
internal/tools/camel-remote.ps1:1878:    # carries every other lane's build. So the plan says so in its own file -- `time_sensitive =
internal/tools/camel-remote.ps1:2788:# A plan that declares `time_sensitive = true` reads durations, and a duration measured beside
```

三行都是**既有的说明句**，逐行说明：

- `README.md:390` 描述 `-Jobs` 那个参数在时间敏感运行里的样子（这计划独占整台主机时预算怎么算），是文档，不是读法；
- `camel-remote.ps1:1878` 是那段注释的开头，解释「为什么声明写在计划里而不是写在这张表里」——它说的是这个字段
  该写在哪，不是怎么读；
- `camel-remote.ps1:2788` 解释为什么 `-NoGpuWatch` 之外还要看时间敏感：这段运行量的是时长，只能和同处境的时长比。

**这一道没有在 `internal/tools` 里新增一处提及**，也没有一行在读这个文件：闸门那一侧调用的是
`--print-time-sensitive`（连字符），读法在 `test/tools/`。要核对这件事，上面这条 `git grep` 就是那个读数。

## 撤掉了什么，为什么

| 文件 | 为什么撤 |
|---|---|
| `test/tools/time-sensitive-reader.mjs` | 第二个读法。它的存在就是第二份真相 |
| `internal/tools/camel-remote-timesensitive.ps1` | 把闸门那两段正则搬出来好被比对；搬出来的是要撤的那半 |
| `internal/tools/camel-remote-timesensitive.test.ps1` | 给两个读法对账；没有两个读法就没有这张账 |
| `test/tools/time-sensitive-reader.test.mjs` | 测的是上面要被删掉的那个读法 |

`feat/tools/selfchecks` 里那两条 entry、`test/plan-selection.json` 里那条规则、`internal/tools/README.md` 里那两行、
自检台账里那个计数（`internal/tools/*.test.ps1` 从二十回到十九），都一起撤了：撤销不是留一条没人读的旧账。

## 这一页不覆盖的两条

1. **时间敏感分支在主机上会卡住**（2026-11-25 实测）：拿到 `hold taken on build-1, build-2 and test-1, test-2,
   test-3` 之后，这次运行自己的 `camel-run plan` 又去排 test 槽（「1 and 2 are held, and slot 3 is the merge
   seat's alone, so this run queues rather than borrow it」），十几分钟不动。这与本页的读法无关，已单独报给
   决策者。
2. **`--print-time-sensitive` 没写进 `docs/`**：这一道的派工写明不要碰 `docs/`。用法与 token 的意思在本页，
   代码里也有整段注释（`test/tools/run-tests.mjs` 的 `printTimeSensitive`）。