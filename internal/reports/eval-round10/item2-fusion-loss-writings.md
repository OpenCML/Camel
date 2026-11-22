# 第 2 项：贪心融合作用于两种写法（`loss-a` / `loss-b`）

**用哪一笔**：`1e6cfcb80309e264a910845dcbaf78fbd2177c55` 的 Release 构建（宿主 `88zjwei` `~/camel-work/eval`）。
作者要求 `2d7c5c3a1`；我**没有重钉回去重编**，理由是两笔构建的**源指纹同为 `0244fca0cbac`（1344 个文件）**
两笔之间只差 `docs/` 与派工提交，`src/`、`stdlib/`、`modules/` 逐文件摘要相同（`build-info.json.fingerprint`）
⇒ 同一个二进制，读数可标为「与 `2d7c5c3a1` 同源」。**报告里以 `1e6cfcb80` 记**，不与 `2d7c5c3a1` 混。

**融合 pass（Camel 写，未改一字）**：`test/cases/opt/writings/greedy_fuse.cml.in`
（按 `greedy_fusion_writings.py` 的做法复制进 scratch 目录作 `greedy.cml`，因为它是 `.in` 模板、不该在树里当 `.cml`）。
它取 `nodes(g)` 给出的乘法，按该序两两配对，把每对的第二个 `replace` 成第一个，再 `commit`——**它问的只有 `nodes` 的顺序**，
不读 `id`/`name`/`origin`。所以两份写法是否得到同一份规范图，就是「`nodes` 是否给了规范顺序」的直接读数。

**程序**：`internal/reports/eval-round10/case-study-variants/loss-a.cml` 与 `loss-b.cml`
（两份只差 `loss` 里 `let total` / `let count` 两行的先后；两份**原文件未改**，只复制到 scratch 里跑）。

| | 字节 | md5 | sha256 |
| --- | --- | --- | --- |
| `loss-a` 未融合 | 26097 | `68940aecd0dc6ac1fa8da5cd00ec28e5` | `740b7a29f1b50b059261569df38bac889657ca76fea6467c62826e837a1e9bd1` |
| `loss-b` 未融合 | 26097 | `68940aecd0dc6ac1fa8da5cd00ec28e5` | `740b7a29f1b50b059261569df38bac889657ca76fea6467c62826e837a1e9bd1` |
| `loss-a` 融合后 | 26097 | `5bff8788844488252b09a72f054c3a5c` | `83c00574b7ab22043c1b9872e340ca138467e30afd94b37ed865406137c078ef` |
| `loss-b` 融合后 | 26097 | `5bff8788844488252b09a72f054c3a5c` | `83c00574b7ab22043c1b9872e340ca138467e30afd94b37ed865406137c078ef` |

- **两份融合后逐字节相同**（md5、sha256 都同）⇒ **无第一处差异可报**。
- **融合确实动了图**：融合后与融合前不同（同字节数、不同 md5/sha256），所以「两份相同」不是「什么都没做」的空读数。
- 两次 `greedy::fuse` 的 commit 都**接受**（exit 0，无 `RuntimeDiag`），融合前后的**程序输出未变**：
  `loss before: 3.7500 / loss after: 0.0000 / [[1.9999925], [-0.99998784]]`。

命令（宿主，scratch 目录里放 `greedy.cml` 与两份 cml，`CAMEL_PACKAGES` 指向 scratch）：

```bash
camel loss-a.cml std::jgir canonical=true            # 未融合
camel loss-a.cml greedy::fuse std::jgir canonical=true   # 融合后（loss-b 同）
```

## 附：作者假设的「并列」在树上不存在

`camel inspect --ties` 对 `train`、`writings/swapped`、`case-study-variants/train-swap` **都是**
`0 group(s) wider than one node, widest 0, over the limit 0 (kMaxTieWidth 6)`；
六个写法（`train`/`rename`/`swap`/`inline`/`loss-a`/`loss-b`）的规范文档**已经逐字节相同**
（sha256 `740b7a29…`），反例 `train-reorder-output` 不同（`4d5ab9a2…`）且**应该**不同（打印次序换了）。
⇒ **「常量编号修复」这个前提不成立：没有可修的并列；六份已同，无需修复。**
若作者认为别处还有并列，请给出 `--ties` 能看到的那一处（或他用的是别的检查——本页只认 `inspect --ties`）。

## 附：第 2 项（pass 过 commit）已有读数，供删留决定

`case-study-variants/train.cml` 上九个 pass 全部 commit **接受**、程序输出**相同**：
`opt::fold`（26097→35994 字节）、`opt::cse`/`opt::dce`/`opt::saturate`、`std::cxx::fold`（→35763）、
`std::cxx::cse`/`dce`/`inline`（→49554）/`saturate`。
唯一**拒绝**在模型之外：`std::cxx::inline` 在 `showcase/c2-mlp-mnist/train.cml` 上 exit 1，原文
`error[RuntimeDiag::PassEffectOrder]: 'std::cxx::inline' committed a graph whose conflicting effects are not ordered:
node 86 of graph 'main' (:struct/copy_arr) … has no order against node 140 … (fs:read_bytes)`；
不带该 pass 时同一程序 commit 通过 ⇒ 那条无序边是 inline 的输出引入的，**真拒**（pass 的图缺了程序原本有的次序），
不是检查误判。完整的 pass × 程序表与两个手写 pass（`probes/fusion.cml`、`probes/checkpoint.cml`）的读数另行交付。