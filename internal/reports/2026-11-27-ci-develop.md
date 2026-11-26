# 2026-11-27 收工：GitHub Actions 四个工作流在集成尖端上的读数

**跑在哪个提交上**：`53956dd377`（当晚的 `integration` 尖端）。**不是** `develop`
（`6d7d0e422c`，落后 682 笔）：`develop` 只在晋级时从 `integration` 快进，收工这一轮要的是
“最新代码在真 runner 上什么样”，所以按 `integration` 派发，并在下面每条读数里写明这一点。
`ci.yml`、`build-pypi.yml`、`perf-baseline.yml` 都能 `workflow_dispatch` 任意 ref；
`ci-leg.yml` 不能（只有 `workflow_call`，它是 `ci.yml` 的三条腿）。

## 一、四个工作流

| 工作流 | run | 时间 | 结论 |
| --- | --- | --- | --- |
| `ci.yml` | `45868710337` | 01:33:17 → 01:59:01（25 分 44 秒） | failure —— **只红在 `Test` 步**，三平台 `Build (Release)` 全 success |
| `ci-leg.yml` | —— | —— | 不能单独派发；今晚以 `Build and test (<platform>)` 三个 job 的形式出现在上面那一跑里 |
| `build-pypi.yml` | `45868703120` | 01:33:14 → 02:08:18（35 分 04 秒） | **success**（`publish_to=none`，未发布） |
| `perf-baseline.yml` | `45868716932` | 01:33:20 → 02:02:29（29 分 09 秒） | failure —— 只红 `map_chain` 一条，根因见第四节 |

## 二、`ci.yml` 的三条腿

```
Source headers                                       success
Build and test (linux-x64)    Build (Release) success   Test (smoke + feat) failure
Build and test (win-x64)      Build (Release) success   Test (smoke + feat) failure
Build and test (macos-arm64)  Build (Release) success   Test (smoke + feat) failure
ASan + UBSan (linux-x64, Debug, smoke)                skipped（dispatch 未勾）
Build and test (linux-x64-nojit)                      skipped（dispatch 未勾）
```

**`win-x64` 的 `Build (Release)` 是这一轮要紧的那一格**：当晚稍早一次 Windows 断编译
（`std::span<const EditNote> edits = {}` 在不完整类型上做指针算术，MSVC 在声明处实例化）
在构建机上修好后，这一跑在**真的 Windows runner** 上确认能编。

### 三腿 `[SUMMARY]`（原文）

```
win-x64     total: 9699, pass: 9100, fail: 545, skip: 21, known failures: 33
linux-x64   total: 9699, pass: 9094, fail: 550, skip: 22, known failures: 33
macos-arm64 total: 9699, pass: 8885, fail: 757, skip: 24, known failures: 33
```

### 逐名比：零新增红

与当天 00:54 的 `c1c7be173` 那一跑逐名比，**三腿一致：added 0、gone 6**：

```
gone: loops.collect.large.{nvm,fvm,jit,prl}    ← loop_collect 那笔
      gpu_probe.once_per_process               ← 同批
      docs.links.questions_index               ← questions index 重生成
```

**mac 腿比 linux 多 188 个失败名**（646 对 458），而**上一跑也是 188**（652 对 464）：
同一个差值 ⇒ 平台既存量，不是本次引入。

**层次差**：CI 的 `[SUMMARY]` 是 **9699** 条，本地 fast 层是 **9740** 条 ——
两层选的东西不同，**这两个数字不能直接对平**，写在一起时必须注明。

## 三、`build-pypi`：四个轮子建成、发布未发生

```
build-wheel (linux-x64) / (linux-arm64) / (windows-latest, win-x64) / (macos-latest, macos-arm64)  全 success
Refuse an unpublishable tag (5.37 item 1)  四个平台全 success
smoke-install 3.9 body-only / 3.10 / 3.14  三条全 success
Publish to TestPyPI (trusted publishing)   completed / **skipped**
Publish to PyPI (trusted publishing)       completed / **skipped**
```

两个发布 job 的 `if:` 分别是

```
publish-pypi      : …(github.event_name == 'workflow_dispatch' && github.event.inputs.publish_to == 'pypi')
publish-testpypi  : if: success() && github.event_name == 'workflow_dispatch' && github.event.inputs.publish_to == 'testpypi'
```

两者都是 `skipped` ⇒ 那一跑的 `publish_to` 既不是 `pypi` 也不是 `testpypi`，只能是 `none`
（派发时显式写的就是 `publish_to=none`）⇒ **没有任何发布动作发生**。

## 四、`perf-baseline`：唯一的红，既存多日

失败步 `Measure`，只有 `map_chain`（`fib.deep`、`alloc_small` 都过）。CI 日志只有

```
[baseline] attempt 1/2/3 of `camel --timeit std::cxx::inline/3 …/map_chain.cml --arg n=2000 --arg reps=20 --output-format json` exited with 1
camel … exited with 1 every time
```

**根因（在构建机上用同一条命令行切开的）**：

```
--timeit <pass>/1             → exit 0
不带 --timeit（直接运行）      → exit 0，stdout `map_chain total=75799`
--timeit <pass>/2 或 /3       → exit 1，JSON 里 failed:true、iteration:2
   0x0300000F / SemanticDiag::NoMatchingFunction / stage=typecheck
   `No matching function found for argument types: <(i: int) => (int, int) pure> (Range).`
换 pass（std::cxx::inline / std::nvm / std::fvm / std::jit）一样
换用例（test/cases/loops/hof_block_alloc.cml）一样
```

⇒ **同一进程里的第二次迭代**解析/类型检查时，`(i: int) => (int, int)` 被读成“多返回值/元组”，
`map` 的 `(item: int) => U` 对不上。**既存多日**：11-24、11-25、11-26 的 `perf-baseline` 都红，
10:06Z 那一跑的命令行与失败一字不差；两棵旧树的同一二进制同样复现。

**这一跑没有出到基线数字**，所以本页不写任何基线值；真要写也只能是“同机 `[load]` 100 下的同机比值”。

## 五、工具事实与例外（下一班要接着用的）

```
· `ci-leg.yml` 只有 `workflow_call`，无法单独 dispatch；它是 `ci.yml` 的三条腿
· `test/tools/perfbaseline/baseline.py` 把 camel 的 stderr 吞了：CI 日志只剩 “exited with 1 every time”，
  真正的拒绝话在 stdout 的 JSON 里 ⇒ 这是“多日红没人发现”的直接原因，值得修
· `--timeit <pass>/N` 的 `/1` 绿、`/N≥2` 红；`/N` 是必填语法，校验本身没错
· `Test` 步的红与本地那批既存红同源（CI 的 feat 层比本地 fast 层更宽），不是本次引入
```

## 六、结论

**零新增红**；**三平台 `Build (Release)` 全绿**（Windows 那次断编译确认修好）；
**发布未发生**（`publish_to=none`，两个 publish job 均 skipped）；
**唯一的红是既存多日的 `--timeit` 第二次迭代**，已作为一条独立的车道活去做（先证后改：
打印两次迭代的类型对象，再修进程级复用那个根因）。