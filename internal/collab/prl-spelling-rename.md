# `std::prl` 是那个调度遍唯一的名字（`D-1128-122`）

**车道**：`lane-block-hash`，枝 `lane/prl-spelling`（基 `origin/integration`，2 笔：`e6f276bef1` 改名、`aa6052b2a0` AST 黄金重录）。**未推。**

## 改名范围

一个遍原来有四个拼法：`std::taskflow`（注册名）、`std::tf`、`std::parallel`，以及 `std::prl`（别名）。留 `std::prl`：

- 声明：`src/stdlib/std.cml` 的 `export func taskflow::` → `prl::`，参数表（`linear`、`fanout_all`、`diag`、`segments`、`bytecode`、`gc_stats`、`gc_stress`、`blocking_leaves`）从此挂在注册名上；
- 注册与后端标签：`src/stdlib/std/prl/taskflow.cpp` 的 `PassKernel("std::taskflow"…)` → `"std::prl"`，`setBackend` 跟着；
- 别名：`src/kernel/execute/pass.cpp` 删三行（`std::parallel`、`std::tf`、`std::prl` 三条都指向 `std::taskflow`）。**最后一条是"空壳"**：注册名改成 `std::prl` 之后 `{"std::prl","std::prl"}` 是指向自己的别名，一并删净；`scripts/camel-pass-pipelines.mjs` 里同样的自别名也删了；
- 调试器：`src/tools/debugger/src/run.cpp` 里 `pass == "std::tf" || "std::prl" || "std::parallel"` 收敛成一个 `pass == "std::prl"`；
- 其余 60 个文件只是拼法本身（20 个 plan 及其 `desc`/`expect_*`、cases 与 harness 的后端列表、benchmarks 的 `CONFIGS`/`SCHEDULING`/`SCHEDULING_PASSES`/`P12`、`cmake/TaskflowWorkerStack.cmake` 的提示串、`docs/cli.md`+`docs/cli.cn.md`、`docs/technical/**`、`AGENTS.md`、`.vscode/launch.json`、`docs/design/cli/README.cn.md:46` 那行"每个遍只留一个名字"）。

**只改名字，没动语义**；**`std::run` 没碰**（它是另一格）；中间机械替换把 `docs/design/effects/plan.cn.md` 的"`std::prl` 就是 `std::taskflow`"变成了"就是 `std::prl`"，已手改为"就是并行调度遍"。

读数：改前改后各跑一次 `camel <case> std::prl bytecode=false`，两边 `PASS ×2`（改后 `BUILD_EXIT=0`）；`dirty 0` 的一条：`test/plans/feat/opt/pass_params.plan.toml` ⇒ `[SUMMARY] total: 60, pass: 60, fail: 0`，`[provenance] tip e6f276bef1… | … | jobs 2`（无 `dirty`）。

## `internal/` 里的 40 处**不改**

`git grep -E 'std::tf\b|std::parallel|std::taskflow'` 在 `internal/` 里还剩 40 处（23 + 3 + 14，非 `internal/` 为 0）。那是**记录**：某次读数当时看到的判词、当时读的表、当时写的回执。把它们改成新名字，等于让一份读数说它当时没说过的话——这和历史记录该有的样子相反。要追"这个拼法什么时候消失"，看提交历史，不是改档案。

## AST 黄金必须重录（硬要求，已做）

`src/stdlib/std.cml` 的声明改名会改它的 AST，所以 `test/plans/feat/parse/golden/ast_hashes.txt` 那行必须跟着走，否则枝一进合并位就红一条：

```
-9377d87dcf46ea44 ok src/stdlib/std.cml
+f4479626c879b315 ok src/stdlib/std.cml
```

重录前 `feat/parse`：`frontend.ast_record` 红，`ast hashes: 4720 file(s) read, 1 stale (1 mismatch, 0 missing, 0 gone)`，`[SUMMARY] total: 131, pass: 130, fail: 1`；重录后 `131/131, fail: 0`，exit 0。重录用 `-UpdateAstHashes -AstOnly 'src/stdlib/std.cml'`，只动这一行（其余 4492 行原样）。

## 留给别人的两件

- 20 个改过的 plan 没有逐个跑（`load` 45，`D-1128-123` 说 load < 40 才取新读数），覆盖交给晋级整层；红了按名字找回。
- `test/plans/feat/opt/pass_params.plan.toml` 的用例**名**仍是 `valid.taskflow_parameters`（`desc` 已写 `std::prl`）。用例名是账上的身份（红表按名字认），改名会让一个绿名从账上消失，所以留在原处；下一轮要清就单独一笔。
> 2026-11-28 收尾：lane/prl-spelling = e6f276bef1（改名 61 文件）+ a6052b2a0（AST 黄金 1 行），dirty 0。dirty 0 读数为 pass_params.plan.toml 60/60 全绿；feat/parse 重录前 4719? → 1 stale（frontend.ast_record FAIL）、重录后 131/131/0。
