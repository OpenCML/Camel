# 融合 pass 在两笔钉上的读数（`2d7c5c3a1` 与 `1e6cfcb80`），均带 `--require-one-form`

**两笔各自的构建**（都在宿主 `88zjwei`，Release，本机不编译）：

| 钉 | build-info revision | 源码指纹 | 构建 | 时刻 |
| --- | --- | --- | --- | --- |
| `2d7c5c3a1` | `2d7c5c3a14`（clean） | `0244fca0cbac`（1344 文件） | `BUILD_EXIT=0`，`BUILD_SECONDS=2.5` | 2026-11-19 ~05:50Z |
| `1e6cfcb80` | `1e6cfcb803`（dirty 0） | `0244fca0cbac`（同 1344 文件） | `BUILD_EXIT=0`，build 206.4 s | 2026-11-19 ~06:05Z |

两笔的源码指纹相同 ⇒ **同一个二进制**；下面 14 个读数在两笔上逐一相同，不是巧合。

## 1. `--require-one-form` 的严格读数（两笔都跑）

```
python3 test/cases/opt/writings/greedy_fusion_writings.py --camel ./out/latest/bin/camel --require-one-form
```

- `2d7c5c3a1`：`the two writings of one program are ONE canonical form: PASS`
  `the greedy fusion is not a no-op, a rename fuses to the same graph, and a different constant does not: PASS`
- `1e6cfcb80`：**同样两行 PASS**。

⇒ **`b1ac24306` 那个「两种写法得两张图」的现象在 `2d7c5c3a1` 上已经不在**（丙.1 生效），新钉上也不在。

## 2. `loss-a` / `loss-b` 的融合（`greedy::fuse`，`greedy_fuse.cml.in` 未改）

| 钉 | 文件 | 字节 | md5 | sha256 |
| --- | --- | --- | --- | --- |
| `2d7c5c3a1` | loss-a 未融合 | 26097 | `68940aecd0dc6ac1fa8da5cd00ec28e5` | `740b7a29f1b50b059261569df38bac889657ca76fea6467c62826e837a1e9bd1` |
| `2d7c5c3a1` | loss-b 未融合 | 26097 | `68940aecd0dc6ac1fa8da5cd00ec28e5` | `740b7a29f1b50b059261569df38bac889657ca76fea6467c62826e837a1e9bd1` |
| `2d7c5c3a1` | loss-a 融合后 | 26097 | `5bff8788844488252b09a72f054c3a5c` | `83c00574b7ab22043c1b9872e340ca138467e30afd94b37ed865406137c078ef` |
| `2d7c5c3a1` | loss-b 融合后 | 26097 | `5bff8788844488252b09a72f054c3a5c` | `83c00574b7ab22043c1b9872e340ca138467e30afd94b37ed865406137c078ef` |
| `1e6cfcb80` | 同上四行 | 26097 | `68940aec…` / `5bff8788…` | `740b7a29…` / `83c00574…`（**逐字节与 `2d7c5c3a1` 相同**） |

两份**融合后相同**，且融合前后不同（`fuse changed the graph`）⇒ 读数非空。两次 `greedy::fuse` 的 commit 都**接受**（exit 0）。

## 3. 六个写法（`case-study-variants/`，文件原样复制到 scratch，未改）

`2d7c5c3a1` 上（`1e6cfcb80` 上逐字节相同，见第 1 项报告）：

| 文件 | 字节 | md5 | sha256 |
| --- | --- | --- | --- |
| `train` | 26097 | `68940aecd0dc6ac1fa8da5cd00ec28e5` | `740b7a29f1b50b059261569df38bac889657ca76fea6467c62826e837a1e9bd1` |
| `train-rename` | 26097 | `68940aec…` | `740b7a29…` |
| `train-swap` | 26097 | `68940aec…` | `740b7a29…` |
| `train-inline` | 26097 | `68940aec…` | `740b7a29…` |
| `loss-a` | 26097 | `68940aec…` | `740b7a29…` |
| `loss-b` | 26097 | `68940aec…` | `740b7a29…` |
| `train-reorder-output` | 26097 | `33556fbe2b760c9c00e5ef053231e583` | `4d5ab9a2a3f6868401ba2de52a053f17d71d2daccb7ce3cfb7d8e54bbd9d8631` |

六份相同，`train-reorder-output` 不同且**应该**不同（打印次序换了，order 边随之换向）。

## 4. 结论

- **不存在可修的并列**（`camel inspect --ties` 在 `train`、`writings/swapped`、`train-swap` 上都是
  `0 group(s) wider than one node, widest 0, over the limit 0 (kMaxTieWidth 6)`）⇒ 无需修复 ⇒ **P′ = `2d7c5c3a1`**。
  新钉 `1e6cfcb80` 与它**源码指纹相同**，读数也逐项相同，所以「新定版再跑一遍」的结果就是上表。
- 引脚差异：**零**。14 个读数（7 份写法 × 2 笔钉）逐一相同；融合后的四份也逐一相同。
- 命令（两笔相同）：`camel <file> std::jgir canonical=true`；融合：把 `greedy_fuse.cml.in` 复制成
  scratch 里的 `greedy.cml`、`CAMEL_PACKAGES` 指向 scratch，然后 `camel loss-a.cml greedy::fuse std::jgir canonical=true`。