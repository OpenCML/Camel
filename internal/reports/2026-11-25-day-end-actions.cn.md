# 日终四支工作流：`develop@2a33df7094` —— **这一棵不是晋级后的树**

写于 2026-11-26 00:30（收工，D-1125-29）。四支里**三支起来了、三支都红**；第四支 `CI leg` **dispatch 不了（HTTP 422）**，没跑成。
下面每一行都是读到的东西；读不到的集中在 §6，不当绿也不当红。

## 1. 靶：`origin/develop` 是 `2a33df7094`，**不是晋级后的树**

```
> git -C D:\Projects\Camel fetch origin
> git ls-remote origin refs/heads/develop refs/heads/integration refs/heads/main
2a33df70949e5148ecc12ed035d39b3b8a9a9ab1   refs/heads/develop
1b7b986117de7e8c1d22f6714d9e21db9a5855d7   refs/heads/integration
67829a34a52dd7eab751806b95f6c8db442dceff   refs/heads/main
```

判「是不是晋级后的树」用四条读数，不是用印象：

| 读数 | 结果 |
|---|---|
| `git merge-base --is-ancestor origin/integration origin/develop` | 退出 1 —— develop **不含** integration |
| `git merge-base --is-ancestor origin/develop origin/integration` | 退出 0 —— integration 含 develop |
| `git rev-list --count origin/develop..origin/integration` | **68** |
| `git -C D:\Projects\Camel reflog show develop` 首行 | `develop@{0}: merge origin/integration: Fast-forward`（本地 develop 已到 `1b7b986117`，`origin/develop` 还停在 `2a33df7094`） |

⇒ **这一轮跑的 `2a33df7094` 不是晋级后的树**：第六次晋级的本地快进已经做了、记录还在补，`origin/develop` 没到 integration 尖端。
`gh workflow run --ref develop` 只能打到 `origin/develop`，所以这一轮**没有别的靶可选**，四个工作流全部打在 `2a33df7094` 上（三支 run 的 `headSha` 都是它，见 §2）。

第六次晋级的两笔清红也在 integration、不在 `origin/develop`（这就是 §3 里 19 条 `opt.pass_*` 的来历）：

```
93ab748d73 in origin/develop: False    in origin/integration: True
65d8689ef4 in origin/develop: False    in origin/integration: True
```
`93ab748d73`（`selftest: the two passes a case declares itself are compiled into libcamel again`，合入为 `c99d7e74fe`）动的是 `src/stdlib/std/selftest/params_test_passes.cpp` 与 `src/stdlib/test/module.cpp` —— 正是 `test::params` / `test::declared` 那两个遍。

## 2. 四支的 run id 与结论

| 工作流（`gh workflow list --all` 实测名） | 文件 | run | head | 结论 |
|---|---|---|---|---|
| `CI` | `ci.yml` | [45646619121](https://github.com/OpenCML/Camel/actions/runs/45646619121) | `2a33df7094` develop | **failure**（3 个 platform job 全红） |
| `Build and package (PyPI wheel)` | `build-pypi.yml` | [45646640382](https://github.com/OpenCML/Camel/actions/runs/45646640382) | `2a33df7094` develop | **failure**（2 个 smoke 红 + 1 个矩阵项 cancelled） |
| `Performance baseline` | `perf-baseline.yml` | [45646663445](https://github.com/OpenCML/Camel/actions/runs/45646663445) | `2a33df7094` develop | **failure**（`Measure` 红） |
| `CI leg` | `ci-leg.yml` | **无 run** | — | **没跑成**，HTTP 422（见 §5） |

派工原文照抄（`publish_to` 的输入名是**从工作流文件里查出来的**，不是默认的）：

```
> gh workflow run "CI" --ref develop --repo OpenCML/Camel
https://github.com/OpenCML/Camel/actions/runs/45646619121
> gh workflow run "Build and package (PyPI wheel)" --ref develop --repo OpenCML/Camel -f publish_to=none
https://github.com/OpenCML/Camel/actions/runs/45646640382
> gh workflow run "Performance baseline" --ref develop --repo OpenCML/Camel
https://github.com/OpenCML/Camel/actions/runs/45646663445
```

`build-pypi.yml` 的 `on.workflow_dispatch.inputs` 只有一项：

```yaml
      publish_to:
        type: choice
        options: [none, pypi, testpypi]
        default: none
```

`publish_to=none` 是**显式**传的；由此两个 `Publish …` job 的结论是 `skipped`，不是「跑了没发」。`gh workflow view <id>` 本身不打印 inputs，输入名取自文件本体。

各支的时间（UTC）：`CI` 15:34:06 → 16:17:10；`PyPI wheel` 15:34:13 → 16:07:03；`Performance baseline` 15:34:20 → 15:55:25。

## 3. 红的逐条

### 3.1 `CI` —— `Test (smoke + feat)`（三个 platform job 的第 19 步，名同）

fast 层读数（每支一份 `summary.json`，同一棵树、同一 tier、`jobs=4`）：

| 腿 | total | pass | fail | skip | xfail | 红计划数 |
|---|---|---|---|---|---|---|
| linux-x64 | 9587 | 8887 | **649** | 20 | 31 | 90 |
| win-x64 | 9587 | 8890 | **647** | 19 | 31 | 90 |
| macos-arm64 | 9587 | 8691 | **844** | 21 | 31 | 114 |

`Source headers` 绿；`ASan + UBSan` 与 `linux-x64-nojit` 是 `skipped`（两个输入默认 `false`，本轮没开）。

**与 11-24 那一轮（`d6503a197c`）按名集比，新红 38 条**（linux 32 / win 34 / macos 38，三条腿上有 30 条重合）。
全部 2244 行（含每条腿的既有红）在 `internal/reports/2026-11-25-day-end-actions-reds.tsv`，列是 `腿 / 计划 / case / NEW|RED`。

新红逐条（`工作流 · 步骤` 对每一行都是 `CI · Test (smoke + feat)`）：

| 计划 | case | 腿 | 一句话原因 |
|---|---|---|---|
| compile.chain | sync.pure_call_is_arranged_freely | L W M | 用例自己的 writing-freedom 自检拒绝：`group=chainpure reference=first expect=identical`，camel 退出 1 |
| compile.chain | sync.pure_call_has_no_edge_to_the_statement_beside_it | L W M | 同上，`group=chainpure_edges reference=first`，退出 1 |
| compile.chain | sync.effect_call_stays_on_the_chain | L W M | 同上，`group=chainpure_effect reference=first expect=differs`，退出 1 |
| compile.chain | sync.faulting_call_stays_on_the_chain | L W M | 同上，`group=chainpure_fault reference=first expect=differs`，退出 1 |
| compile.diag_json | passes_never_run_text | L W M | 期望「程序不执行」，实际 `RuntimeDiag::PassesNeverRun` 只是 Warn、程序照跑，verification 的 `the program is not executed` 不成立 |
| compile.diag_json | passes_never_run_json | L W M | 同上的 json 形态 |
| compile.diag_render | render.werror_no_position | L W M | golden 与实况不符：stderr 现在带着 `error[RuntimeDiag::PassesNeverRun]: Pass 'std::nvm' answered with an empty graph …` |
| compile.higher_order_clause | node_role_reads_the_fact | L W M | `camel` 退出 1，用例期望 0（verification: `camel exited 1`） |
| compile.region_roots | region_roots.writing_freedom_of_two_writes | L W M | `group=region_write reference=print_first expect=same-graph`，退出 1 |
| exemplars | 02_branch_shape_contract | L W M | `RuntimeDiag::TensorDimensionMismatch`，`exemplars: all checks passed` 不出现 |
| modules.tensor | op_stats.table.fvm | L W M | 期望 stderr 有 `tensor op stats:` 表头与 `zeros/add/multiply/sum` 行，实际只有 `8.0` |
| modules.tensor | op_stats.table.nvm | L W M | 同上 |
| opt.pass_function | declared.default_absent | L W M | `RuntimeDiag::PassNotExportedByModule`：`Module 'test' declares 'declared' but does not export it` |
| opt.pass_function | declared.function_value | L W M | 同上（`test::declared cost=10 …` 与 `camel-ran-marker` 都没出现） |
| opt.pass_function | declared.multi_segment_module | L W M | 同上 |
| opt.pass_function | declared.names_the_module_path | L W M | 同上；期望的 `RuntimeDiag::InvalidPassParameter` 被它挡住 |
| opt.pass_function | declared.no_module | L W M | 同上 |
| opt.pass_function | declared.no_such_function | L W M | 同上 |
| opt.pass_params | declaration.every_parameter_by_name | L W M | `RuntimeDiag::PassNotProvidedByModule`：`Module 'test' provides no pass 'params'` —— 入口目录的 `test.cml` 遮住了库的 `test` 模块 |
| opt.pass_params | declaration.lists_array_and_nullable_types | L W M | `PassNotExportedByModule`（`declared` 未 export），同一遮蔽 |
| opt.pass_params | declaration.same_machinery_as_a_call | L W M | 走到 `SemanticDiag::ParamArgumentMissing`，`pass parameters match calls: PASS` 不出现 |
| opt.pass_params | declared.empty_array | L W M | `PassNotExportedByModule`，同一遮蔽 |
| opt.pass_params | declared.int_array_written | L W M | 同上 |
| opt.pass_params | declared.null_default_is_omitted | L W M | 同上 |
| opt.pass_params | declared.null_written | L W M | 同上 |
| opt.pass_params | declared.nullable_string_takes_the_word | L W M | 同上 |
| opt.pass_params | default.folded_constants | L W M | `PassNotProvidedByModule`，verification `two calls print two lines, got 0` |
| opt.pass_params | error.array_element_not_int | L W M | `PassNotExportedByModule`；期望的 `InvalidPassParameter` 不出现 |
| opt.pass_params | error.int_where_int_array_declared | L W M | 同上；期望的 `ParamArgumentWrongType` 不出现 |
| opt.pass_params | error.missing_required | L W M | `PassNotProvidedByModule`；期望的 `ParamArgumentMissing` 不出现 |
| opt.pass_params | error.null_where_not_nullable | L W M | `PassNotExportedByModule`；期望的 `ParamArgumentWrongType` 不出现 |
| trans.cpp | rc19_seed70_builds_after_specialize | L W M | 期望 `PASS: 1 of 1 programs agree with std::fvm`，输出里是 `FAIL` |
| consistency.comprehension | comprehension.shadowed.jit | **M** | `RuntimeDiag::JitWithoutCodeGenerator`：arm64 无 JIT 代码生成器，这条新 Warn 让 `.jit` 与 `.nvm` 的 diagnostics 不再相同 |
| linear.fvm_config | jit.defaults | **M** | 同上：`[fastvm] config jit=OnDemand hot_threshold=1 …` 一行没打印（Warn 顶掉了） |
| linear.fvm_config | jit.hot_threshold_is_the_one_given | **M** | 同上（`hot_threshold=0` 那一条） |
| tools.selfchecks | selfchecks.merge_readings_cache | **W M** | `FAIL a cold --read is a miss and measures`；Windows 那条 stderr 只剩 git 的 LF→CRLF 警告 |
| tools.selfchecks | selfchecks.oversight_reload | **M** | 期望 `ok: 23/23 checks`，实际在那之前就 FAIL |
| transforms | math_scalar_gradient_through_the_library.nvm | **W M** | 期望输出含 `erf`/`erfc` 的 `d/dx=` 行，实际只到 `sqrt`（Windows 只缺 `erfc`） |

**既有红（不是这一轮新增）**：linux 617 / win 613 / macos 806 条，逐条在 tsv 的 `RED` 行里。它们昨天在 `d6503a197c` 上也是红的，本轮没有变化。

### 3.2 `Build and package (PyPI wheel)` —— 全是 `smoke-install` 的内联夹具

| job | 步骤 | 一句话原因 |
|---|---|---|
| `smoke-install (python:3.10-slim, interop)` | #5 `Install from the wheel inside python:3.10-slim` | 工作流自己 printf 出来的 `/shared/rules.cml` 还是旧拼写 `export func double_totals(values: f32[]): f32[] {`，`camel.load()` 抛 `SyntaxDiag::UnexpectedToken`（`/shared/rules.cml:1:41`）——`eb20efab7b` 之后返回类型只写在 `=>` 后 |
| `smoke-install (python:3.14-slim, interop)` | #5 同上 | 同一份夹具、同一条诊断（3.14 与 3.10 逐字相同） |
| `build-wheel (macos-latest, macos-arm64)` | —— | **cancelled**：排队 15 分钟没拿到 `macos-latest` runner（`runner_name` 为空、`steps` 为空），谁发起的取消 API 里读不到（§6） |

绿的部分：`build-wheel` linux-x64 / linux-arm64 / windows-latest 三个矩阵项 success；`smoke-install (python:3.9-slim, body-only)` success；`Publish to TestPyPI` / `Publish to PyPI` 按 `publish_to=none` skipped。

### 3.3 `Performance baseline` —— `Measure` 一步红

| job | 步骤 | 一句话原因 |
|---|---|---|
| `Graph-store baseline (Linux x64)` | #12 `Measure` | `test/cases/perf/gc/map_chain.cml` 用 `camel --timeit std::cxx::inline/3 … --arg n=2000 --arg reps=20` 三次尝试**每次都退出 1**，`baseline.py` 最后整段回放 stderr 是空的 ⇒ 没有诊断文本可抄（§6）。前两个用例 `fib.deep`、`alloc_small` 是过的，`Upload the table` 仍 success（`if: always()`） |

构建、`Conan install`、`Upload the table` 都绿 —— 这一支**只有 Measure 这一格**。

## 4. 与 11-24 那一轮（`d6503a197c`，run `45432504419`）的差

| 腿 | 11-24 | 11-25 | 新红 | 修好 |
|---|---|---|---|---|
| linux-x64 | 1205 红 / 130 计划 | 649 / 90 | 32 | 588 |
| win-x64 | 1206 / 131 | 647 / 90 | 34 | 593 |
| macos-arm64 | 1390 / 147 | 844 / 114 | 38 | 584 |

两轮**总数不同**：`d6503a197c` 是 9898 例，`2a33df7094` 是 9587 例（少 311）。所以上面比的是**名集**（case 全名），不是总数相减；少的 311 例去了哪里没查，不猜。
净效果是红少了（−556 / −559 / −546），而**新红 38 条**是这一轮真正要进 backlog 的东西 —— 绿的定义是「没有新红」，不是「红的总数少了」。

## 5. 没跑成的那一支：`CI leg`

```
> gh workflow run "CI leg" --ref develop --repo OpenCML/Camel
could not create workflow dispatch event: HTTP 422: Workflow does not have 'workflow_dispatch' trigger
    (https://api.github.com/repos/OpenCML/Camel/actions/workflows/393651133/dispatches)
```

原因在文件里：`ci-leg.yml` 的 `on:` **只有 `workflow_call`**（6 个 input：`runner` / `slug` / `arch` / `msvc_runtime_version` / `nojit` / `experimental`），没有 `workflow_dispatch`。它是被 `ci.yml` 以 `uses: ./.github/workflows/ci-leg.yml` 调用的可复用工作流（`ci.yml` 第 99、113 行两处）。

⇒ **它没有独立 run id，也不可能有**。它今天确实执行了，执行体是 `45646619121` 里的三个 job（`Build and test (linux-x64)`、`(win-x64)`、`(macos-arm64)`），结论写进 §3.1；`linux-x64-nojit` 那个调用是 `skipped`。
要不要给 `ci-leg.yml` 加 `workflow_dispatch`（好让「四支各跑一轮」这件事真的成立）是决策者的事，本轮不动工作流。

## 6. 读不到的（不是绿，也不是红）

- **`map_chain` 为什么退出 1**：`baseline.py` 第 210–219 行把三次尝试的退出码写出来后，第 218 行 `sys.stderr.write(result["stderr"])` 回放最后一份 stderr —— 日志里这一段**是空的**，即 `camel` 退出 1 而 stderr 无输出。要定因得单独复现那一行命令；本轮不构建、不复现，所以不写原因。
- **macos 那个 wheel 是谁取消的**：API 只给 `conclusion=cancelled`；`runner_name` 空、`steps` 空，说明它没被派到 runner 就被取消。同一 workflow 同一 ref 只有一个 run（`gh run list --workflow build-pypi.yml --limit 6` 核过），concurrency 组不解释这一格。
- **`tools.selfchecks.merge_readings_cache` 在 Windows 上到底断在哪**：Windows 那条 case 的 stderr 只有 `warning: in the working copy of 'README.md', LF will be replaced by CRLF`，真正的失败行被 git 的警告顶掉了；macOS 那条能看到 `FAIL a cold --read is a miss and measures`。
- **`op_stats.table.*` 的 `8.0` 是什么**：三个平台一致 `exit=0`、stderr 只有 `8.0`，统计表整段没打印。它在 11-24 那轮不红，这一轮三条腿同时红；原因在 `std::stats` 侧还是用例侧，本轮没查。

## 7. 复现这一轮

```powershell
git -C D:\Projects\Camel fetch origin
git -C D:\Projects\Camel rev-parse origin/develop                      # 2a33df7094…
gh workflow run "CI" --ref develop --repo OpenCML/Camel
gh workflow run "Build and package (PyPI wheel)" --ref develop --repo OpenCML/Camel -f publish_to=none
gh workflow run "Performance baseline" --ref develop --repo OpenCML/Camel
gh workflow run "CI leg" --ref develop --repo OpenCML/Camel             # 预期 422，不是失败
gh run download 45646619121 --repo OpenCML/Camel -n test-logs-linux-x64 -D <dir>
```

红的机器清单：`internal/reports/2026-11-25-day-end-actions-reds.tsv`（`腿 / 计划 / case / NEW|RED`，2244 行）。
本轮读数一律来自这三支 run 的 job 日志与 `test-logs-*` 产物；本机没有构建、没有跑计划。