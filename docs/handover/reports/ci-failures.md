# CI 一直在失败：每一处红的原因（H407，2026-10-24）

## 一句话

CI 只在打标签时跑（`develop` 上的合并一次都不跑），而 10-23/10-24 develop 攒下的东西里有一条让
**全部** Windows 构建编译不过的包含顺序错误，加上它自己那一层里两条只在这个 runner 上成立的红
（ccache 把缓存写进检出被审计当成源文件；无 CUDA 头的构建里断言了一次根本不会发生的探测）——
标签一推，两个工作流同时全红。

## CI 跑的是哪一层：**快层**

`ci-leg.yml:231` 的测试步骤是 `node scripts/test.js`（无参数），目标是 `test/plans/smoke` +
`test/plans/feat`（`test/runner.mjs:41-44`），tier 默认 `fast`（`test/tools/run-tests.mjs:533`）。
run `40077350582` 的 linux-x64 腿自报，逐字：

    [INFO] discovered plans: 209
    [INFO] tier: fast, tests: 5956, jobs: 4
    [SUMMARY] total: 5956, pass: 5940, fail: 2, skip: 14

**所以“慢层那 8 条已知红导致 CI 一直失败”被日志推翻**：慢层（2922 条）从没进过 CI，
`fuzz_lossless_tree` / `churn_prl` / `progen_check` / `tree_training` / `resnetbn_train_step_export` /
`pass_export_equals_export_model` 在 CI 日志里出现 **0 次**。CI 的 2 条红都在快层。

## 失败面（近 40 次运行）

CI 20 次（13 失败、5 取消、2 成功）、PyPI 15 次（9 失败、4 取消、2 成功）、Dependency Graph 5 次全绿；
**Performance baseline 一次都没跑过**（`gh run list --workflow=perf-baseline.yml` 返回 `[]`）。

| 工作流 | job / 步骤 | 红 | 逐字原因（尾） | 归因 | 处方 |
|---|---|---|---|---|---|
| CI | Build and test (linux-x64) 与 (linux-x64-nojit)，`Test (smoke + feat)` | 红 | `plan-selection: 1394 tracked file(s) match no rule and are not an allowed rule-less class: .ccache/*(none) (.ccache/0/0/66s81k5ll0dfdoen1v90k5dtpjosk8oR and 1377 more); .ccache/*.TAG (.ccache/0/CACHEDIR.TAG and 15 more)` | (a) 仓库缺陷：ccache 的 `cache_dir` 在检出内，`.gitignore` 没收它 | 已修 `cce903af6` |
| CI | 同上，同一步骤 | 红 | `verification exited 1` / `verification output: Error: expected one device probe, saw 0` | (b) 构建能力：CI 三腿都 `-- tiles: source inspection only (CUDA Toolkit not found)`，无后端就不探测 | 已修 `e988977ad`（前置条件，不是放宽断言）；要不要在 CI 装 CUDA 头见 `questions/ci-cuda-toolkit.md` |
| CI | Build and test (win-x64)，`Build (Release)` | 红 | `...um\tlhelp32.h:32:1: error: unknown type name 'HANDLE'` … `fatal error: too many errors emitted` … `20 errors generated.` / `[FAIL] Error executing command: cmake --build . --config Release -- -k 0` | (a) 仓库缺陷：`7baf8ad58` 把 `tlhelp32.h` 放在 `windows.h` 之前 | 已修 `ceba06c47`；**本机无法复核，只能在远端验证** |
| PyPI | `build-wheel (windows-latest, win-x64, 3.10/3.11/3.12/3.13)` 四个 job，`Native Release build + wheel` | 红 ×4 | 同上一处逐字 + `Error: Command failed: cmake --build . --config Release` | (a) 同一处缺陷 | 同一处修复 |
| PyPI | `build-wheel (ubuntu-latest, linux-x64, 3.x)` | 绿 | — | — | — |
| CI + PyPI | 共 9 个 run `cancelled` | 不是红 | `concurrency: cancel-in-progress: true`（`ci.yml:23-25`）：同一 ref 上再投一次就取消前一个 | (c) 基础设施/操作 | 无需改；看板上那些红里有 9 个是这种 |
| CI | @10-21 develop，win-x64 `Test` | 红 | `[FAIL] nested_return.compile_time` … `verification output: Error: unexpected output:` ；`[FAIL] fs.remove_permissions` … `strictEqual actual: 1, expected: 0` ；`[FAIL] native_state_checkpoints` 同形 | (c) 环境相关（Windows ACL、案例挂起） | develop 上其后已改（`ad6250c59`、`ddf6a5a93`、`396beb06c` 都已在 `b5bc264e3` 里）；**本机不能复核**，下一次 win-x64 腿见真章 |
| PyPI | @10-16、@10-21 develop，Windows ×4 | 红 | `ConanException: Visual Studio Runtime version (v140-v145) not defined. Please, add the compiler.runtime_version=[v140-v145] setting to your profile.` | (c) 基础设施：runner 镜像的 MSVC 版本 | 已由 H57 `767a89c59` 修；10-21 16:33 的 run `39635884376` 全绿 |
| CI + PyPI | @10-21 标签 v0.2.21/22/23 | 红 | `relocation R_X86_64_TPOFF32 against '_ZN5camel4core12effect_trace12_GLOBAL__N_113tlsSiteOriginE' can not be used when making a shared object; recompile with -fPIC` | (a) 仓库缺陷（当时标签分支的状态） | 已被 develop 上的提交消除 |
| CI | @10-16 develop，四腿各 63 条 | 红 | `[FAIL] gir.gir_snapshot` / `node count mismatch actual=39, expected=42` | (a) 仓库缺陷（迁移中途的快照漂移） | 已消除 |
| CI | @v0.2.28 linux 两腿 | 红 | `docs.links.citations`：`src/core/rtdata/print.cpp:113: 'docs/handover/questions/reduced-precision-text.md': no such file` / `2 unverified citation(s)` | (a) 仓库缺陷 | develop 已改指 `docs/handover/reviews/questions-2026-10-23.md`；v0.2.29 的 CI 里已绿 |
| Performance baseline | — | — | **0 次运行** | 覆盖空洞 | `questions/ci-triggers-and-coverage.md` |

## 改了什么，以及本地等价复现

| 提交 | 改 | 本地等价复现（逐字） |
|---|---|---|
| `cce903af6` | `.gitignore` 收 `.ccache/` | 在检出里放 `.ccache/0/{CACHEDIR.TAG, 66s81…R, 66s81….TAG}`：**改前** `git ls-files --cached --others --exclude-standard` 里 3 条 `.ccache/*`，审计分类出 `.ccache/*(none)` 与 `.ccache/*.TAG`（与 CI 的分类同形）；**改后** 0 条、`uncovered file(s): 0` |
| `ceba06c47` | `modules/process/operators.cpp`：`windows.h` 提到最前 | 包含闭包脚本：`the tlhelp32.h include is at line 25` / `no <windows.h> found above the tlhelp32.h include: 97 repository header(s) walked, 0 reached windows.h`（**Windows 工具链本机没有，编译只能在远端验证**） |
| `e988977ad` | 新增 `test/tools/have-cuda-build.mjs`，给 `gpu_probe.once_per_process` 加前置条件 | 主机三分支：真实构建（CUDA 12.3 头）`exit=0`；替身“有后端” `exit=0`；替身“无 CUDA 头” stdout `this build of camel has no CUDA back end (tiles was built without the CUDA Toolkit headers): nothing probes a driver, so a test of a probe line cannot hold`，`tool exit code: 1`。**无 CUDA 头的真实构建本机产生不了**（`find_package(CUDAToolkit QUIET)` 在此主机必成功），其触发串取自 `capability.cpp` 的无 `CAMEL_TILES_CUDA` 分支，与 CI 三条腿的 `-- tiles: source inspection only` 对应 |
| 三处合起来 | 主机跑 CI 的同一层：`-Plan smoke,test/plans/feat -Tier fast -Jobs 8` | `[PASS] runner.backend_matrix 19.67 s`、`[PASS] gpu_probe.once_per_process 972.05 ms`（**PASS 不是 SKIP**：断言在有后端的构建里没有被削弱）、`[SUMMARY] total: 6015, pass: 6000, fail: 0, skip: 11, known failures: 4`、`RUN_EXIT=0`、`wall: 243.3 s, jobs: 8` |

## 问询（不在本车道决定）

- `questions/ci-triggers-and-coverage.md`：触发器要不要加 `develop`、标签要不要跑慢层/全量、已知红要不要
  允许失败、要不要加机器或分片——附每种的代价。
- `questions/ci-cuda-toolkit.md`：Linux 腿要不要装 CUDA Toolkit 头文件，让 tiles 的 CUDA 路径真的被测。

## 没做的事

**没有推送一个字节、没有用 `gh` 触发任何远端运行、没有改任何远端设置**；只本地提交三个（`cce903af6`、
`ceba06c47`、`e988977ad`）。没有改触发器、层数、`jobs`，没有标记任何已知红为允许失败，没有动
`docs/handover/README.cn.md`、`reviews/**`、`docs/design*`。Windows 编译、Windows 上的测试、以及
无 CUDA 头的构建，本机都无法产生，已逐条标注。