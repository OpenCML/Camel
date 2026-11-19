# 第 3b 项：规范图规模（新钉 `1e6cfcb80`）+ 「交换两处 let」到底差在哪

**树**：`D:\Projects\Camel-wt\eval`，`lane-eval`，**已重钉到 `1e6cfcb80`**（`c4746823d` 是派工提交，
`1e6cfcb80` 把六份写法拷进树）。上一版报告在 `2d7c5c3a1`。
**构建**：Release，`Camel v0.2.45 (Build 261119_051158_2d7c5c3a14)`，
revision `1e6cfcb80309e264a910845dcbaf78fbd2177c55`，源码指纹 `0244fca0cbac`（1344 文件，与提交一致），
dirty 2 项但**都不是源码**（`dirtyPaths` = 本报告目录下的 `item1-…md` 与 `probes/`，未跟踪），builtAt `2026-11-19T05:21:12Z`，宿主 `88zjwei`。
**负载**：32 核，`/proc/loadavg` = `25.79 29.26 30.31`。

## 1. 六个写法（树里新放的 `case-study-variants/`）的规范图

命令：`camel <file> std::jgir canonical=true`（与上一版同一套）。边数 = 节点 `inputs[]` 引用求和；区域 = `graphs[*].regions` 条数。

| 文件 | 图 | 节点 | 边 | 区域 | 字节 | sha256（前 16） |
| --- | --- | --- | --- | --- | --- | --- |
| `train.cml` | 5 | 57 | 72 | 1 | 26097 | `740b7a29f1b50b05` |
| `train-rename.cml` | 5 | 57 | 72 | 1 | 26097 | `740b7a29f1b50b05` |
| `train-swap.cml` | 5 | 57 | 72 | 1 | 26097 | `740b7a29f1b50b05` |
| `train-inline.cml` | 5 | 57 | 72 | 1 | 26097 | `740b7a29f1b50b05` |
| `loss-a.cml` | 5 | 57 | 72 | 1 | 26097 | `740b7a29f1b50b05` |
| `loss-b.cml` | 5 | 57 | 72 | 1 | 26097 | `740b7a29f1b50b05` |
| `train-reorder-output.cml` | 5 | 57 | 72 | 1 | 26097 | `4d5ab9a2a3f68684` |

- **六份字节数与 sha 都相同**：`train`、`rename`（改名）、`swap`（`run` 里两处 `let` 互换）、
  `inline`（把 `step` 写进 `run`）、`loss-a`/`loss-b`（`loss` 里两处 `let` 互换）。
  这六份在新钉上**是同一条规范文档**。
- **第七份 `train-reorder-output` 不同，而且就该不同**：它把 `println(loss after)` 与 `println(loss before)`
  调了次序，输出次序变了，图里两处打印之间的 order 边随之换向——不是编号问题。

## 2. 定位：「两处可互换的 let 得到不同编号」——并列在哪、tie-break 用的是什么

先给结论：**在这两份程序里，规范编号的并列表根本没被走到**。

```
$ camel inspect --ties examples/case-study/train.cml
canonical ties: 0 group(s) wider than one node, widest 0, over the limit 0 (kMaxTieWidth 6)
$ camel inspect --ties examples/case-study/writings/swapped.cml
canonical ties: 0 group(s) wider than one node, widest 0, over the limit 0 (kMaxTieWidth 6)
$ camel inspect --ties docs/handover/reports/eval-round10/case-study-variants/train-swap.cml
canonical ties: 0 group(s) wider than one node, widest 0, over the limit 0 (kMaxTieWidth 6)
```

唯一的并列决策点在 `src/runtime/canonical.cpp:440-541`（`smallestTable` / `arrangeEqualKeyRun`），它现在的 tie-break 是三级：

1. `nodeKey`（`canonical.cpp:608`）：kind + 算子 URI / 常量文本 / 端口角色 / fill、loop 种类 + 值类型 +
   **已经编号的输入号** + 绑定对（按值号排序）。注释写明"不写源里拼的东西：不是名字、不是位置、不是 NodeId"。
2. 键相等的那一段，枚举排列、取 `tableOf`（`canonical.cpp:545`）最小者；上限 `kMaxTieCandidates = 4096`、
   `kMaxTieWidth = 6`（`canonical.cpp:44-47`）。
3. 段宽超过 `kMaxTieWidth`（或已在枚举里）时，按 `fragmentKey`（`canonical.cpp:569`，自身的表 + 已编号读者的号）
   做 **`std::stable_sort`**（`canonical.cpp:513`）——**相等片段保留"进来的次序"**，而"进来的次序"是 `tied` 的次序，
   即 `refs_` / 语句次序，也就是书写次序。

第 3 步是这棵树里**唯一**还会让书写次序漏进编号的地方（一个潜在的、尚未被任何例子触发的缺陷）；但本次两份文件
`ties` 都是 0 组，第 1、2 步就把顺序定完了，**不是这里**。

那么 `train.cml` 与 `writings/swapped.cml` 差在哪？差在**图本身**，不在编号：

- 两份文档都是 5 图 / 63 节点 / 93 边 / 29945 字节，**第一次差异在 `g1` 的第 4 个节点**：
  同一位置一份是 `array[4]`（`y`），另一份是 `array[8]`（`X`）；随后 `types` 表两项、`FUNC` 节点的类型号跟着换。
- 两份都是 `let X = [...] as f32[4,2]` 与 `let y = [...] as f32[4,1]` 各经一个 `tensor:reshape@1.1`。
  规范文档里能直接读到：**`train` 中 `X` 的 reshape（`n8`）没有 control 前驱，`y` 的 reshape（`n9`）带
  `control`+`sync` 指回 `n8`；`writings/swapped.cml` 里恰好相反**（`n8` 成了 `y`，`n9` 是 `X`）。
  也就是说，两处 reshape 之间那条"时间线"的**方向随声明次序翻了**。
- 这条链不是编号造出来的：reshape 按保守分类可能出故障，按设计原则落在时间线上；两颗常量谁先声明，谁先上时间线。
  **换编号规则改不掉它**——即使把两个常量按值排序编号，`y` 的 reshape 在 `train` 里仍带那条 control 边、
  在 `swapped` 里仍不带，文档照旧不同。

**⇒ 判据结论：`writings/swapped.cml` 与 `train.cml`「不该相同」。** 它们交换的是两个**可能出故障**的操作在时间线上的
次序（先失败的是哪一个会变），属于语义不等价；作者在 `2026-11-17` 的裁定里要求张量运算按保守分类上时间线，
这条链就是那条裁定的结果。按"不许改等价关系"的禁令，我没有改动任何规范形代码，也没有为了让它们相等去动
`smallestTable` 的键。

而这批**新的**六份写法（论文实际量的那六份）在新钉上已经是同一条文档：`swap` 换的是 `run` 里 `before` 与 `model`
两处**不上时间线**的 `let`，正是"应当相同、现在相同了"的那一类。

## 3. 与上一版（`2d7c5c3a1`）的差异

| 项 | 上一版 | 本版 |
| --- | --- | --- |
| 钉 | `2d7c5c3a1` | `1e6cfcb80`（+`c4746823d` 派工提交） |
| 被测文件 | 树里当时**只有四份旧写法**（`examples/case-study/writings/{renamed,swapped,moved}.cml`） | 作者放的**六份写法**（`case-study-variants/`，7 个文件） |
| 程序文本 | 63 节点 / 93 边 / 2 区域 / 29945 字节 | 57 节点 / 72 边 / 1 区域 / 26097 字节 |
| 结果 | `renamed` = `train`；`swapped`、`moved` 不同（当时无法判定该不该同） | 六份同 sha；`train-reorder-output` 不同且应不同；**`swapped` 的差异已定性为「不该相同」（§2）** |
| 旧四份在新钉上 | — | 读数与旧钉**逐字节相同**（`renamed` = `f3978b42`，`swapped` = `f2fa36d4`，`moved` = `66138fb5`）：钉的变化没有改这三份的输出 |

一句话：**新六份全部符合判据**（5 同 + 1 应异）；剩下唯一"字节同、sha 异"的旧 `swapped` 是语义不等价，不是编号不稳定。

## 4. 复现

```bash
cd ~/camel-work/eval
export CAMEL_HOME="$PWD/out/latest"; export PATH="$CAMEL_HOME/bin:$PATH"
V=docs/handover/reports/eval-round10/case-study-variants
for f in train train-rename train-swap train-reorder-output train-inline loss-a loss-b; do
  camel $V/$f.cml std::jgir canonical=true > /tmp/$f.json
  echo "$f $(wc -c < /tmp/$f.json) $(sha256sum /tmp/$f.json | cut -c1-16)"
done
camel inspect --ties examples/case-study/writings/swapped.cml   # 0 组
```
节点/边/区域就是把每份 JSON 按 §1 的定义求和；原始输出见 `NOTES.md` §4。