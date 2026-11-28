# 未合 `lane/*` 名单（只读盘点）

读数时间：2026-11-28 12:1x。`origin/integration = 09acc5fe89`（2026-11-28 12:03:21 +0800）。
口径：`未合` = `git rev-list --count origin/integration..<branch>`；`落后` = `git rev-list --count <branch>..origin/integration`；`cherry` = `git cherry origin/integration <branch>`（`-` = 该笔在尖端有等价补丁，`+` = 独有）。
**本轮只读**：不建枝、不删枝、不打标签、不推。

## 一、总表（35 条，按落后降序）

| 枝 | 未合 | 落后 | 最后一笔 sha | 日期 | 距今 | 最后一笔标题 | 分类 | 提案 |
|---|---|---|---|---|---|---|---|---|
| `lane/pass-registry` | 0 | 1257 | c1e051fd5b | 11-27 15:48 | 20h23m | reports: the alias that lets `std::opt` be typed is named as a branch, not as the tree | 已合未删 | 存档后删 |
| `lane/test-conditions` | 0 | 1112 | e0fa69c0f8 | 11-27 19:02 | 17h09m | test: ask the build, not the platform, whether a backend or a tool exists | 已合未删 | 存档后删 |
| `lane/arg-parse-uaf` | 0 | 1046 | 98ad9cff1e | 11-27 19:10 | 17h00m | backlog: D-1127-179 -- the merge seat batches the lanes that have their readings… | 已合未删 | 存档后删 |
| `lane/main-exit` | 0 | 962 | c327a87216 | 11-27 20:46 | 15h24m | test: the snippet check states its cost, and a doc insertion moves one golden line | 已合未删 | 存档后删 |
| `lane/review-1127` | 0 | 771 | 225647424c | 11-27 23:59 | 12h11m | nn: the window of a convolution or pooling operator has no default outside its declaration | 已合未删 | 存档后删 |
| `lane/bang-suffix` | 0 | 725 | 5c60cc61bd | 11-27 23:49 | 12h21m | Merge docs/fetch-first: the merge seat fetches before it merges… | 已合未删 | 存档后删 |
| `lane/transpose-fix` | 0 | 646 | 30d1b01be6 | 11-28 00:57 | 11h13m | jvp: the lifted loop owns its condition region | 已合未删 | 存档后删 |
| `lane/native-constas` | 0 | 488 | 3bcf3934d0 | 11-28 04:50 | 7h20m | constas: the record and the rebuild follow the tree they are merged into | 已合未删 | 存档后删 |
| **`lane/editview-commit`** | 0 | 333 | e0e28a7570 | 11-28 09:38 | 2h32m | The rewrites keep opening the draft, because erasing inside the block does not run | 已合未删 | 存档后删（我上一轮的枝） |
| `lane/main-effects` | 0 | 330 | ee550f53dc | 11-28 08:04 | 4h06m | main: the entry declares no value, and the returns that carried one go with it | 已合未删 | 存档后删 |
| `lane/opt-inline` | 0 | 320 | 979224dec7 | 11-28 09:52 | 2h18m | test: only the corpora that lacked a byte comparison get one | 已合未删 | 存档后删 |
| `lane/walk-abort-377` | 0 | 201 | 6504b06293 | 11-28 10:53 | 1h17m | collab: the key-table readings -- slice is five and six words… | 已合未删 | 存档后删 |
| `lane/draft-ref-oob` | 0 | 104 | 0967afd615 | 11-28 10:48 | 1h22m | backlog: batch five lands, the gate that refused it is named, and a stray write is undone | 已合未删 | 存档后删（我 task-420 的空枝，内容已在尖端） |
| `lane/call-candidates` | 0 | 80 | 1dcabf9cde | 11-28 11:10 | 1h00m | gir: ask the declaration resolver whether a signature takes the arguments | 已合未删 | 存档后删 |
| **`lane/asan-segment-hooks`** | 0 | 51 | 6ff0727b02 | 11-28 11:27 | 43m | ASAN regions for the spaces, so a write into memory they took back is reported where it happens | 已合未删 | **不要碰**（你点名） |
| `lane/conditional-samples` | 1 | 336 | 20673f1e1e | 11-28 07:47 | 4h23m | rewrite: give every scalar parameter its sample in the rule check | 落后>100 | **存档后删**：`cherry` 是 `+0 / -1`，唯一那笔在尖端有等价补丁 ⇒ 内容已吸收 |
| `lane/onnx-g2` | 4 | 333 | 9cea23c74e | 11-28 08:02 | 4h08m | onnx refusals: four entries measured a call the inliner no longer leaves… | 落后>100 | 换底（我已换过一次到 9cea23c74e，零冲突；读数 147/117/29 待同口径对照）或交所有者判弃 |
| `lane/pass-decl-3` | 15 | 330 | c457eec80e | 11-28 08:15 | 3h55m | probe t323: the pass-by-pass probe is gone, its case stays for the driver probe | 落后>100 | 换底（15 笔全 `+`，内容独立） |
| `lane/blocks-scope` | 9 | 330 | b29e41fc32 | 11-28 08:09 | 4h01m | records: the rows this lane moved, written one file at a time | 落后>100 | 换底（我已换到 b29e41fc32，parse 快层只剩已知 `frontend.ast_record`）|
| `lane/commit-of` | 15 | 314 | 6c76c16726 | 11-28 08:35 | 3h35m | collab: the lane's final base and what was re-measured on it | 落后>100 | 换底 or 判弃（15 笔全 `+`；与 `task-374`「删 `commit_of`、按 URI 造 `commit`」相关，建议交决策者） |
| `lane/task-229` | 16 | 314 | a019b10fa4 | 11-28 08:35 | 3h35m | collab: residual_mlp's judgement, the three readings behind it… | 落后>100 | 换底（16 笔全 `+`） |
| `lane/onnx-run` | 29 | 308 | 6fcf353b2e | 11-28 09:31 | 2h39m | collab: the survey figure is 86 real calls, not 109 | 落后>100 | 换底（29 笔全 `+`；它是 `task-368` 族的活枝，优先） |
| `lane/floatgate-state` | 5 | 308 | dfde1e6451 | 11-28 09:41 | 2h29m | collab: the loop state cotangent never reaches the sweep's aborts | 落后>100 | 换底 |
| `lane/c4-round-source` | 1 | 304 | 1d98238ffd | 11-28 11:16 | 54m | graph: a handle of a finished draft answers a fault, not a crash | 落后>100 | 换底（1 笔，小） |
| `lane/transpose-loop-seed` | 1 | 304 | c12c06f527 | 11-28 10:27 | 1h44m | wip: the replayed round count, as a starting point for task-419 | 落后>100 | 与 `lane/draft-ref-oob-wip` **同 sha** ⇒ 二者只留一条（建议留后者作 `task-420` 的基，前者存档后删） |
| `lane/draft-ref-oob-wip` | 1 | 304 | c12c06f527 | 11-28 10:27 | 1h44m | 同上（`wip/c4-replay` 基，我 `task-420` 用） | 落后>100 | 保留（下一格在这个基上打探针） |
| **`lane/i32-encoding`** | 2 | 273 | 17d3cd1c0e | 11-28 11:55 | 16m | rtdata: drop the Int32 comparison case; the constant and the arithmetic now agree | 落后>100 | **不要碰**（你点名，已冻在队列） |
| `lane/facts-version-key` | 6 | 104 | ec3aa8ab5f | 11-28 12:02 | 9m | facts: a store is told when any graph it named ends… | 落后>100（边界） | 在做（刚提交）⇒ 不动 |
| `lane/key-tables` | 3 | 104 | 4e2b707363 | 11-28 11:31 | 39m | parse: re-record the ast hash of a ruleset that gained a law | 落后>100（边界） | 换底（3 笔，小） |
| **`lane/tensor-pow-law`** | 17 | 56 | 99ea77efe0 | 11-28 12:00 | 11m | types: a number and a bool are their own tangent space… | 中间（<100） | **不要碰**（你点名，按 `D-1128-100` 改写中） |
| `lane/arg-parse-uaf-w` | 11 | 56 | aaae3671cd | 11-28 11:24 | 47m | transforms: the forward derivative of `one_hot` is the zero of the result's shape | 中间（<100） | 在做（你已划走给他人）⇒ 不动 |
| `lane/perf-baseline` | 5 | 46 | 686fbbb5c1 | 11-28 11:35 | 36m | collab: two of the four buffers can be spared, and two of them were never allocated | 中间（<100） | 在做 ⇒ 不动 |
| `lane/deriv-tails` | 1 | 40 | ea8c75725d | 11-28 11:42 | 29m | forward: give `:op/idx_take` the forward law the other two reads of the bracket form have | 中间（<100） | 在做 ⇒ 不动 |
| `lane/std-run` | 4 | 37 | 020952acfd | 11-28 11:46 | 24m | parity: the transform's own recognitions and the no-tangent rule are not debt | 中间（<100） | 在做（你已划走）⇒ 不动；`cherry +1 / -3` |
| **`lane/loop-tape-values`** | 1 | 2 | 0ed8967a43 | 11-28 12:02 | 8m | onnx: the carried-array refusal says what this exporter does not yet do | 中间（<100） | **不要碰**（你点名，刚换到 `0ed8967a43`） |

## 二、分类小计

- **已合但枝没删（未合 = 0）**：15 条 —— `pass-registry`、`test-conditions`、`arg-parse-uaf`、`main-exit`、`review-1127`、`bang-suffix`、`transpose-fix`、`native-constas`、`editview-commit`、`main-effects`、`opt-inline`、`walk-abort-377`、`draft-ref-oob`、`call-candidates`、`asan-segment-hooks`（最后这条不要碰）。
- **落后 > 100 且未合**：15 条 —— 见上表；其中 `conditional-samples` 的 `cherry` 是 `+0 / -1`（**内容已被吸收，可存档后删**），其余 14 条 `cherry` 全是 `+`（内容独立）。
- **中间（落后 < 100）**：5 条 —— `tensor-pow-law`（不要碰）、`arg-parse-uaf-w`（在做）、`perf-baseline`（在做）、`deriv-tails`（在做）、`std-run`（在做）、`loop-tape-values`（不要碰）。※实为 6 条，`tensor-pow-law` 与 `loop-tape-values` 已点名。

## 三、提案（只提案，不执行）

1. **14 条"已合未删"存档后删**（打 `archive/<枝名>-20261128` → `-D`）：`pass-registry`、`test-conditions`、`arg-parse-uaf`、`main-exit`、`review-1127`、`bang-suffix`、`transpose-fix`、`native-constas`、`editview-commit`、`main-effects`、`opt-inline`、`walk-abort-377`、`draft-ref-oob`、`call-candidates`。（`asan-segment-hooks` 虽已合，但你点名"不要碰"，不动。）
2. **`lane/conditional-samples` 存档后删**：`cherry +0 / -1` ⇒ 唯一的补丁在尖端已有等价物，删掉不丢语义。**这一条是唯一能减闸门的"删"**。
3. **`lane/transpose-loop-seed` 与 `lane/draft-ref-oob-wip` 同 sha（c12c06f527）** ⇒ 只留 `draft-ref-oob-wip`（`task-420` 的基），另一条存档后删 ⇒ **又减 1 条闸门**。
4. **其余 13 条未合且 `cherry` 全 `+` 的**：按所有者确认处置 —— 在做 ⇒ 换底；已弃 ⇒ 存档后删。**我不猜谁在做**（见第五节）。

## 四、闸门算术

- 当前：`refs/heads/lane/*` 共 **35** 条，其中**未合 = 20** 条（与"闸门 20/21"对得上）。
- 第 1 条提案（14 条已合未删）**不减闸门**：它们本来就按"未合 = 0"不占额度，删了只减枝数 35 → 21。
- 能减闸门的是第 2、3 条：**20 → 18**。
- 若所有者对第 4 组里"已弃"的给出名单，每确认一条再 −1（**目前可确认的只有上面 2 条**）。

## 五、我判定不了的（缺信息，不猜）

1. **每条的"谁在做"**：你上一条说"我会给所有者名单"，但本轮没收到 ⇒ 我只能用"最后一笔时间"旁证（≤1h 的都在活跃），不能据此断言归属。
2. **`lane/onnx-g2` / `lane/blocks-scope` 的处置**：这两条我已换底过（9cea23c74e / b29e41fc32），但它们是否需要继续跟进取决于 419/420 的排期，我没有排期信息。
3. **`lane/commit-of`**（15 笔全 `+`）：与 `task-374`「删 `commit_of`、降级按 URI 造 `commit`」同族，是保留还是判弃要决策者定；我按"内容独立"记，不判弃。
4. **`lane/editview-commit`**：`cherry` 我没单独跑（它以"未合 0"归类），若你要按内容确认它的 11 笔是否全被吸收，我再跑一遍 `git cherry` 单独报。