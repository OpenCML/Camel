# 固定流程与命令

## 1. 合并闸门（每次合入 develop 后）

```powershell
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 `
     -Path D:\Projects\Camel-wt\verify -Plan smoke,test/plans/feat -Jobs 8
```

期望：`[SUMMARY] total: ~4750, pass: ~4736, fail: 0, skip: 14`、`RUN_EXIT=0`。
**fail 必须是 0**；skip 是设计内的。出现失败先判断是不是时序假红（GPU 那几个 10 秒超时），
用 `-Jobs 8` 重跑该计划确认，再谈修。

单计划验证（合并前快速看一个任务）：

```powershell
pwsh -NoProfile -File ... -Path D:\Projects\Camel-wt\<wt> -Plan test/plans/feat/modules/graph.plan.toml -Jobs 8
pwsh -NoProfile -File ... -Path <wt> -Plan smoke -NoBuild      # 只跑测试，复用已有构建
```

## 2. 合入一个 handover 分支

```powershell
Push-Location D:\Projects\Camel-wt\verify
git fetch origin
git merge --no-ff handover/Hxx-<slug> -m "<这组改动合起来做了什么>"
# 有冲突：按内容解；文档类双方章节都留，代码两边 case 都留，任务书取最新对账过的表
git add -A; git commit --no-edit
# 闸门
git push origin develop
```

冲突高发文件：`docs/handover/README.cn.md`（任务书，几乎必冲）、`test/plans/feat/modules/graph_facts.plan.toml`、
`docs/technical/39_loops.md`、JIT 后端（`x64_backend.cpp`、`trampoline.cpp`）。

解决冲突后用一个小脚本按"精确字符串替换"改，别手写大段 diff；改完 `Select-String '<<<<<<<|>>>>>>>'` 确认清零。

## 3. fsh-dev 同步（作者的文档/设计/任务书）

```powershell
Push-Location D:\Projects\Camel-wt\verify
git fetch origin
git log --oneline develop..origin/fsh-dev     # 没有就回"fsh-dev 无更新"
git merge --no-ff origin/fsh-dev -m "Merge fsh-dev: <这批文档/设计做了什么>"
git push origin develop
```

- 若只是「一次提交 + 它的 revert」，净变化为空，**不合入**，只记录。
- 合入后读任务书新增/改动的任务，调整派工；若改动了生成物（`docs/diagnostics.md`、AST 金标），
  在合并后的树上重新生成并提交。

## 4. 发布到 PyPI

事实：

- 版本在 `package.json` 的 `version`；`changes.log` 顶部写 `[X.Y.Z]` 与 `Changes:` 列表。
- 工作流 `.github/workflows/build-pypi.yml` + 可复用 `.github/workflows/ci-leg.yml`，
  矩阵 `python-version: ["3.10","3.11","3.12","3.13"]`，每个 job 只用该解释器跑 `npm run pypi:rollup`。
- `CAMEL_TARGET_ARCH: x86-64-v3`（AVX2 + FMA），**绝不 `native`**：wheel 要能在别的 CPU 上跑。
- 发布走 OIDC trusted publisher：仓库 `OpenCML/Camel`、工作流 `build-pypi.yml`、环境 `pypi`。
  `workflow_dispatch` 的 `publish_to` 选 `none`（只构建）/ `pypi` / `testpypi`；**推 `v*` tag 自动构建并发布**。
- Windows leg 的两个已知坑：`export CC=clang CXX=clang++`（不然 `Ninja Multi-Config does not support
  platform specification`）；删掉 `compiler.runtime_version`（不然 `VS non-existing installation: 17`）。

步骤：

1. 闸门绿 + wheel 运行绿（`gh run list --workflow build-pypi.yml`）。
2. 确认版本号（当前 `0.2.24`）与下一个开发版本（`0.2.25`），**这两件事必须问用户**。
3. `changes.log` 的 `[X.Y.Z]` 段确认（起草过就让用户过一遍）。
4. `main` 上打 tag（`vX.Y.Z`）→ 推 → 工作流构建矩阵并发布。
5. 发布后把 `package.json` 版本推到下一个开发版本。

`gh` 以 `Kogler7` 登录；看日志要 `gh api .../logs --allow-escape-sequences` 并剥 ANSI。

## 5. 面板（PanelDeck）

- 源：`D:\Projects\panel-deck\src\PanelDeck\default-panels\camel\panel.html`
- 运行中的 app 读：`C:\Users\Zhenjie\AppData\Roaming\panel-deck\panels\camel\panel.html`
- 没有 .NET SDK，`dotnet build` 跑不了：**复制部署**，`PanelStore` 监视目录、文件一变自动重载。
- 强制重载：`Invoke-RestMethod -Method Post http://127.0.0.1:4520/reload`（或托盘菜单"重新加载面板"）。
- 状态文件：`D:\Projects\Camel-wt\build-state.json` / `test-state.json`，schema：
  `{updatedAt, holder{worktree,config|plan,path,pid,queuedAt,startedAt,done,total,phase,lastLine,remote}, waiters[], recent[]}`；
  `remote: "88zjwei"` 显示 `88`，缺失或 `null` 显示 `本机`。
- 写状态用 `Global\CamelBuildState` / `Global\CamelTestState` 互斥量；写失败只丢一次更新，**绝不能中断构建**。

## 6. 工作树纪律

```powershell
git -C D:\Projects\Camel worktree list
git -C D:\Projects\Camel worktree remove --force D:\Projects\Camel-wt\<wt>   # 分支合入后
pwsh -NoProfile -File ...\camel-remote.ps1 -Path <wt> -Clean                   # 顺手清主机侧
```

留在盘上的只有：主检出、`verify`、以及正在跑的任务。合并完立刻删，别攒（曾经攒到 17 个、
D: 只剩 41 GB）。