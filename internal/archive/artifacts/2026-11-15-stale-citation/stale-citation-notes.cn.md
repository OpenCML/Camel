# `internal/questions/` 过期引用：全树改到 0，外加让检查器对这一类单独出声

日期 2026-11-15。**基线 `origin/develop` = `025d645db`**（主检出当时 HEAD 是它的子提交 `da1c11c9`）。
**主检出 `D:\Projects\Camel` 一次都没被写**：改动全部发生在由 `git archive 025d645db` 解出的
`D:\Projects\Camel-wt\varsurvey\varfix2\`（7826 个文件，`git init` 后 commit 成 base），
两个 patch 都由那份副本 `git diff` 得出，且都在 `025d645db` 上 `git apply --check` 为 0
（在当时的 HEAD `da1c11c9` 上也是 0）。

## 1. 交付物

| 文件 | 内容 | 落地顺序 |
|---|---|---|
| `stale-citation-fixed.patch` | 19 个文件、30 行，每行只有 `questions`→`reviews` 一个词 | **先落这个** |
| `check-doc-links-misplaced.patch` | `scripts/check-doc-links.mjs` 新增 `misplacedIn`（+23 行），对「目录存在但没有该文件」单独报错 | 后落（先落它会让树短暂变红） |
| `fallback-census.diff` / `fallback-census.txt` | 上一轮量 418 用的测量仪器与原始读数（`0692517fc` 快照），仅供复现，**不提交** | — |
| `varfix2/` | 两份 patch 都已打好的副本，可直接读 | — |

## 2. 改了什么（19 文件 / 30 行）

| 文件 | 行 | 说明 |
|---|---|---|
| `diagnostics/registry.tsv` + 生成的 `semantic.cpp` / `docs/diagnostics.md` | 3 | 闭包捕获那条帮助文本（`--write` 重生成，非手改） |
| `modules/transforms/split.cpp` | 1 | 字符串字面量 |
| `modules/transforms/transpose.cpp` | 2 | 注释，路径仍跨两行 |
| `modules/transforms/vmap.cpp` | 2 | 注释 |
| `src/passes/trans/cpp/operator_rules.cpp` | 1 | 注释，仍跨两行 |
| `docs/roadmap.cn.md`、`docs/technical/36_gc_handle_rules.md` | 2 | |
| `docs/test-plans.md` | 1（行内两处：链接文字 + 链接目标） | 目标页在 `reviews/` 里**存在**，不是那条例外 |
| `scripts/check-program-holders.mjs` | 1 | |
| `test/cases/compile/ref/ref_boxes_made_per_call_ten.cml`、`…_thousand.cml` | 2 | |
| `test/plans/feat/compile/{rulesets,state_escape}.plan.toml` | 3 | 含 `rulesets` 那行 `desc` 里的同一页 |
| `test/plans/feat/compile/transforms.plan.toml` | 4 | 全在 `desc` 字符串里 |
| `test/plans/feat/modules/onnx.plan.toml` | 6 | 全在 `desc` 字符串里（同一页 6 次） |
| `test/plans/feat/modules/selected_protection.plan.toml` | 1 | |
| `test/plans/feat/consistency/match.plan.toml` | 1 | |

`git diff --word-diff=porcelain` 的全部 60 条变更行都含 `questions/` 或 `reviews/`，
没有一条是别的改动。`questions/` 这一类（页在 `reviews/`）现在全树 0 处。
`test/plans/**` 里那 11 处是在 `#` 注释之外、`desc` 字符串里——检查器不扫字符串，
是这轮逐行读出来的，上一轮报的 11 处只是「检查器看得见的那一部分」。

## 3. 检查器怎么单独出声（`scripts/check-doc-links.mjs`）

`targetOf` 的 basename 兜底（`:668`）一个字都没动。新加的是一个判据：
**引用带目录前缀 → 该目录（相对引用文件或仓库根）真实存在 → 精确位置没有该文件
→ 而兜底在别处找到了唯一同名文件**，就报一行
`the file is not in that directory; it is at '<找到的位置>'`，计入 `unverified`（exit 1）。

目录不存在的不报（`effects/edges.cn.md`、`camel/core/type/composite/array.h#…`、
`rulesets/transpose.cml` 这类短名写法照旧合法）；没有目录前缀的不报。

### 四条判据的读数（`025d645db` + 两份 patch 的副本里）

| 判据 | 读数 |
|---|---|
| ① 改后全树 `--citations` | `no unverified citation: 942 of 942 symbol citation(s) verified, 2461 path citation(s) across 6519 file(s)`，exit 0，**0 条**这类报告 |
| ② 把 `docs/technical/36_gc_handle_rules.md:36` 改回 `questions/` | **恰好 1 条**：`docs/technical/36_gc_handle_rules.md:36: 'internal/questions/2026-11-06-who-roots-…cn.md': the file is not in that directory; it is at 'internal/reviews/2026-11-06-who-roots-…cn.md'`，exit 1；改回后恢复绿 |
| ③ 418 条短名引用不许误报 | 19 个文件退回基线、只留新检查器：**恰好 12 条**报告，12 条全属 `handover/questions/` 这一类，正好是那 11 处（`docs/test-plans.md:1574` 的链接文字与目标各算一次）；其余短名引用**一条都没有** |
| ④ 其余引用读数逐字节相同 | 同一棵树上旧检查器与新检查器都印 `942 of 942 … 2461 path citation(s) across 6519 file(s)`，逐字节相同 |

另外：`--self-test` 绿；`diagnostics-catalog --check` / `--verify-doc` 绿；
`feat/docs_links` 计划里 6 个 `--citations` 条目退出码全部符合预期（0/0/1/1/1/2）。

## 4. 这一轮之后仍然看不见的四类（记在案，没动）

1. **路径跨两行的引用**（`transpose.cpp:2106/5490`、`operator_rules.cpp:1734`）：写对了，但正则只看见第二行的裸文件名；若哪天被改回 `questions/`，新判据也看不见（它没有目录前缀）。要补，得先让引用解析跨越续行。
2. **字符串字面量 / 围栏代码块 / `.tsv` 里的副本**（`semantic.cpp`、`docs/diagnostics.md`、`split.cpp`、`registry.tsv`）：`isComment`、`isProseExample`、`SCANNED` 各自跳过它们，结构上看不到。
3. **`internal/**` 里的历史记录**（约 110 处）：`RECORD_DIRS` 不扫记录文件，这是设计；没动。
4. **三处同类不同病的残留**（未改，留判）：
   - `test/plans/feat/compile/stated_extents.plan.toml:55` → `2026-11-04-who-types-the-rank-zero-tangent-at-a-cuts-exit.cn.md`，**两处都不存在**（页没写过或已删）；
   - `test/plans/feat/compile/transforms.plan.toml:2297` → `internal/questions/internal/reviews/…`，前缀**叠了一次**（以前某次机械替换的产物），删哪个前缀得人判；
   - `test/cases/compile/transforms/mean_axis1_keepdims_gradient_reads_the_row_length.cml:12` 引了目录但漏掉 `.cn.md`；`transpose_refuses_a_nonlinear_operator.cml:9` 是散文里提到目录名，不是引用。

## 5. 复现

```powershell
$d = 'D:\Projects\Camel-wt\varsurvey\varfix2'          # 已解好 025d645db + 两份 patch
git -C D:\Projects\Camel archive 025d645db | tar -x -C <空目录>
git -C <空目录> init; git -C <空目录> add -A; git -C <空目录> commit -qm base
git -C <空目录> apply D:\Projects\Camel-wt\varsurvey\stale-citation-fixed.patch
git -C <空目录> apply D:\Projects\Camel-wt\varsurvey\check-doc-links-misplaced.patch
node <空目录>\scripts\check-doc-links.mjs --citations                  # 绿
git -C <空目录> checkout -- docs/technical/36_gc_handle_rules.md       # 退一处
node <空目录>\scripts\check-doc-links.mjs --citations                  # 恰好 1 条，exit 1
```