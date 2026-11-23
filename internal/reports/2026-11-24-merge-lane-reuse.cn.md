# 合并位在 `origin/develop` 前进时复用读数

2026-11-24，D-1123-121 的机制落进 `internal/tools/merge-lane.ps1` 之后的两次真跑。这条机制的判据与验证
写在同一处的代码注释里，单元测试在 `internal/tools/merge-lane.test.ps1`；这一页只记那条判据为什么长成这样。

## 为什么要用 `--changed` 而不是路径前缀

作者给的判据是「`--changed` 选出的计划集为空」。写成路径前缀（「`internal/` 下的都是文档」）看起来更简单，
而它与选择规则**会在某个具体文件上分岔**：`internal/tools/merge-lane.ps1` 就在 `internal/` 下，而
`test/plan-selection.json` 给 `internal/tools/` 点名 `feat/docs_links.plan.toml`。按前缀判断的实现会复用那笔
合并的读数，同时两边都看起来对、没有任何一步会失败——这正是要避免的那类错。

## 复用不是假设，是三步核对

1. 把合并重新落到新的 `origin/develop` 上（`Re-LandMerge`，仍在那个丢弃式检出里）；
2. `git diff --name-only <已量树> <重新落的合并树>` 必须 ⊆ 新增那几笔的文件集（`Get-FilesOutside`）；
3. 只有 ② 成立才复用读数推送；否则在新尖端上重新合、重新量（下一轮，同一组闸门）。

另外重新落的树上再跑一次 header 检查：复用的是读数，而「文件缺头」是 CI 的第一步、不是哪个计划的事，
在这里多花一秒是为了让「要推的那棵树」仍然满足它。

## 读数

两次实跑的过程与读数记在车道分支 `lane/merge-reuse` 的交付说明里。

## 读数（第一次真跑，2026-11-24 06:28 CST）

- 宿主机上叶名 `merge` 的 checkout 没有 `build/compile_commands.json`，clang 那一步 `exit 2`，前一句是 `camel-remote` 的 `-NoBuild refused: out/latest has no build-info.json`。
- 基线种子 21 个 case、12 s；行读数 26 s。
- 同一跑暴露：`scripts/merge-record.mjs` 从丢弃式树里取，而它的基比工具早，于是合并是绿的、记录写不出。
