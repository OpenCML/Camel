# 问题：一条本该报出来的诊断，进了 pass 就变成 `RewriteInvalid: std::exception`

事实与读数，不含该修哪儿的判断。开发线 tip `0a1a509f5`。

## 1. 读数

`test/plans/feat/tools/debugger.plan.toml` 的 `debugger.dap_protocol`（`tier = "slow"`）连红 5 次，全部落在 181.4–181.6 s：

```
[FAIL] debugger.dap_protocol   181.47 s / 181.42 s / 181.57 s / 181.57 s / 181.54 s
```

181.5 s 是适配器客户端自己等的：`test/tools/debugger/client.mjs` 的 `WAIT_MS = 60000`，测试里两个 `waitEvent('breakpoint')` 加一个 `waitStop` 各等满 60 s。`meta.json`：`wall_ms 181567.58`、`timeout_ms 300000`、`timed_out false`、`cpu_ms null`。

适配器在 `output` 事件里逐字给出的原因：

```
error[RuntimeDiag::GraphPassFailed]: Pass 'rules::simplify' failed: error RewriteInvalid: std::exception.
help: A pass written in Camel returned an error value instead of a program, and the message is the error's kind and text. The program is left as the previous pass produced it, and the pipeline stops.
exited exitCode 1
```

之后没有任何断点事件，于是 `waitEvent('breakpoint', …)` 得到 null、`waitStop()` 得到 null，`dap_protocol.test.mjs:253` 的 `dap.variables(stop.top.id)` 抛 `TypeError: Cannot read properties of null (reading 'top')`。

## 2. 不用 DAP 的复现

```
printf 'file test/cases/tools/debugger/fixtures/calls.cml\nrun rules::simplify std::nvm\nquit\n' | out/latest/bin/camel-db
```

每次都得到同一句 `Pass 'rules::simplify' failed: error RewriteInvalid: std::exception`，5/5；`std::opt`、`rules::simplify_strict` 也红，`std::nvm` 与 `std::opt::inline` 不红。换成 4 行的 `func main(): int sync { println(1 + 2); return 0 }` 一样红，所以与输入程序无关。同一条 pass 在命令行上 5/5 绿：`camel test/cases/tools/debugger/fixtures/calls.cml rules::simplify std::nvm` 打印 `10`、exit 0。

## 3. 抛出链（gdb，在 `camel-db --run-worker` 子进程里）

```
#0 __cxa_throw (typeinfo for camel::core::error::DiagnosticsLimitExceededException)
#1 camel::core::error::Diagnostics::checkLimits()
#2 camel::core::error::Diagnostics::add(camel::core::error::Diagnostic&&)
#3 camel::core::error::Diagnostic Camel::core::error::DiagnosticBuilder::commit<…>(…)
#4 camel::execute::(anonymous namespace)::CamelPass::apply(camel::runtime::Program const*, std::ostream&)
```

紧接着 stdout 上就是第 1 节那句 `GraphPassFailed`。

## 4. 两处事实

`src/core/error/diagnostics/diagnostics.cpp:603-623` `Diagnostics::checkLimits()`：per-severity 限额是 0 时，第一条件该严重度的诊断**抛出**而不是记下来。`include/camel/core/error/diagnostics.h:120-136`：`DiagnosticsLimitExceededBaseException` 与它派生的 `DiagnosticsLimitExceededException` **都不覆写 `what()`**，所以 `what()` 只剩基类那句 `std::exception`。`modules/rewrite/module.cpp:870`（`rewrite::rewrite`）与 `:945`（`rewrite::run`）用 `catch (const std::exception &e) { return makeError("RewriteInvalid", e.what(), …); }` 兜住它，于是"要报的那条诊断"变成一句没有信息量的 pass 失败。

限额本身不是调试器独有的：`tools/camel-cli/main.cpp:188-191` 给 runtime sink 的也是 `{.total_limit = -1, .per_severity_limits = {{Severity::Error, 0}}}`。调试器路径独有的三件事在 `tools/debugger/src/compile.cpp:33-40`（建 Context 时同一个 `Error, 0`）与同函数第 37 行 `GlobalConfig::SetInspectionMode(true)`，以及 `tools/debugger/src/run.cpp:270` 的 `requireKnownPasses` 走 `UserDefinedModule::compileAsProgram` 现场编译 pass 模块；命令行在这三处都不同。也就是说差别在于：**调试器路径上真的被 commit 了一条 error 诊断**（命令行上同一程序没有），而限额 0 把它变成异常。这条诊断是谁、在哪一行 commit，没有抓到（见下）。

## 5. 要问的

1. `DiagnosticsLimitExceededBaseException::what()` 是否该带上那条诊断的文本（严重度、模块、位置、消息），而不是只剩 `std::exception`？
2. `rewrite::rewrite` / `rewrite::run` 是否该停止把"限额异常"改写成 `RewriteInvalid`——它是控制流，不是规则的语义错误？
3. inspection 模式（`SetInspectionMode(true)`）下"commit 一条 error 诊断"是不是预期行为？如果不是，这条诊断应当怎么处理？

## 6. 没做到的

- 没定位到 pass 内**哪条诊断、哪一行 commit**：gdb 只到 `Diagnostics::add` 这一层（`DiagnosticBuilder::commit` 的调用点没取到）。要拿到，得给 `modules/rewrite/module.cpp` 那两处 catch 下断点，或在 Debug 构建里重跑第 2 节的复现。
- 没在翻转线的 tip 上查这件事（只确认了那条用例在那边同样红，181.64 s）。