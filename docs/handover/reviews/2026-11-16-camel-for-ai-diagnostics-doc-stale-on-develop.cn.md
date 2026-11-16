# `camel-for-ai` 在 develop 上就是红的：这份生成物该由谁重生成、要不要每车道各修一遍？

> 一页一问。**默认建议写死在下面，本轮按默认做完，不另等。**
> 本页由 `lane-e9fix` 在 2026-11-16 记，起因是本车道的 `-Check all` 里出现了一条不属于自己的红。

## 结论（先写这一行）

```
`-Check all` on develop is red for every lane, at `camel-for-ai` and (through it) `gate-static`:
docs/reference/camel-for-ai-diagnostics.md is one diagnostic behind diagnostics/registry.tsv.
The missing one is `IntervalHasNoOpenForm`, added by d3814543b ("refuse the open interval at the
'..' by name"); whoever added it did not run `node scripts/camel-for-ai.mjs --write`.
It is not this lane's: this tree has no diff from origin/develop in that file (last written by
54be326ee, a lane merge today), and the check fails on origin/develop's own content.
The fix is one command and two lines: `node scripts/camel-for-ai.mjs --write`.
```

## 证据（三条，都是可重跑的）

1. **本树与 `origin/develop` 在这个文件上零差异**

   ```
   git diff --stat origin/develop..HEAD -- docs/reference/camel-for-ai-diagnostics.md
   （空）
   ```

   这条就是「不是本车道带进来的」的证据：本车道这两笔只动了
   `internal/tools/camel-remote.ps1`、`scripts/check-e9-full.mjs`、`test/tools/e9diff/*`。

2. **这条红在闸门上的原话**（`pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path <wt> -Check all -NoGpuWatch`）

   ```
   docs/reference/camel-for-ai-diagnostics.md: the 'diagnostics' region is not what its source
   produces (322 entries); run `node scripts/camel-for-ai.mjs --write`
   [camel-remote] --- camel-for-ai: exit 1 ---
   [camel-remote] --- gate-static: exit 1 ---        （同一处，pass static 里包含了它）
   ```

   本地一条命令即可复现，不需要构建、不需要主机：`node scripts/camel-for-ai.mjs --check` → exit 1。

3. **差的是什么**：一条诊断，不是一片

   ```
   node scripts/camel-for-ai.mjs --write
     wrote docs/reference/camel-for-ai-diagnostics.md: 1 region(s), 33442 generated characters,
     1171 hand-written, 34613 in total
   git diff --stat → docs/reference/camel-for-ai-diagnostics.md | 3 ++-
   ```

   ```
   -**Syntax — errors** (`SyntaxDiag`) — 19
   +**Syntax — errors** (`SyntaxDiag`) — 20
   +| `IntervalHasNoOpenForm` | An interval has two ends: 'a..' has no open form, and this '..' has
   +| no right-hand side. |
   ```

   加它的那笔是 `d3814543b feat: refuse the open interval at the '..' by name`（今天，`diagnostics/registry.tsv`
   里已经声明、`SyntaxDiag` 那边已经发得出来），**只是没人重生成文档**。

## 为什么它值得一页

- **它红的是所有车道，不是一条**：`gate-static` 挂在同一处，而 `gate-static` 是静态层的总账。任何车道跑
  `-Check all` 都会看到自己的 run 里有两个红，并且都要花时间去证明「不是我的」——本车道今晚就花了两轮闸门
  （185 s + 391 s）来证明这件事。
- **生成物与生成器不一致，是这类闸门唯一能说的那句话**：文档没有坏，登记表也没有错，**错的是「改了登记表
  却没有重生成」这个动作漏了**。这条漏一次，闸门就红一次，而它红的时候指向的是文档，不是那个漏掉的动作。

## 默认建议（已按此做完，不另等）

1. **按 `diagnostics/registry.tsv → node scripts/camel-for-ai.mjs --write` 重生成，不手改生成物。**
   本车道已经这么做了，改动就是上面那 3 行（`2 insertions, 1 deletion`），单独一笔提交，不掺进 E9 那几笔。
2. **不改 `scripts/camel-for-ai.mjs` 的检查口径**：它现在说的是实话（生成的区域与源不一致），把它改松才是错的。
3. **降低复发**：加诊断的那笔应当顺手 `--write`；若要一道墙拦它，那属于 `agents/commit.md` 的检查表，
   不是本页能定的。**这一条留给语言作者**，本页只把「它今天红、红在哪、怎么修」写清。

## 若作者另有裁定

- **若说「这条红不该由任何车道顺手修，应当退回 `d3814543b` 的作者」**：那就把本页那笔单独提交撤掉
  （只有一行命令的效果，撤掉零风险），红会回到 develop 上，等它的作者重生成。
- **若说「生成的文档不该进仓库，应当由构建生成」**：那是 `docs/reference/` 的组织问题，与这条红无关，
  本页的建议在那种方案下依然成立（那条链仍然是唯一的生成路径）。
## 决策者答复（2026-11-16）

同意：按 `registry.tsv` 重生成，不手改，不放松检查。防复发：加诊断的那一笔自己跑 `--write`；`agents/commit.md` 的检查表加一行"改了 `diagnostics/registry.tsv` 就跑 `camel-for-ai.mjs --write`"，合并闸门里 `-Check all` 本来就会抓到它，合并前不许跳过。
