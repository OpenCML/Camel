# 差分门：阶段 2 的两条合并条件，每条一个命令

阶段 2 的两条接口车道各自要能**用一条命令自证没改行为**。差分本身不新写：它是
`test/tools/diff-compare.mjs` 的（两次构建、同一份计划语料、输出一次、图一次）。这一层只加三件
事，让那句话能直接贴进派工单：

1. **基线写死并核对。** 比较的两边各读一次 `out/latest/build-info.json`；基线那一边不是
   `baseline.json` 里那个提交的干净构建，就 exit 2。**拿错基线是唯一一种「看起来是绿」的失败。**
2. **一行读数**，带着取数的 sha：`[GATE 2.1 view] baseline=2576db796 lane=<sha> cases=<n>
   output_differ=<n> graph_differ=<n> differences=<n> unmeasurable=<n> verdict=GREEN|RED`。
   条目验收要的是 `differences`。
3. **测不到不许报绿。** 语料选空、某一边整片报错、找不到 `[TOTAL]` 行，一律 exit 2 并说原因。
   「对空集来说全都一样」不是读数。

## 两条命令（原样贴进派工单）

```powershell
# ① 先把这条车道的树在主机上构建成 install（差分读的是 install，不是源码树）
pwsh -NoProfile -File internal/tools/camel-remote.ps1 -Path D:\Projects\Camel-wt\<树> -Plan <这条车道的计划> -Jobs 8

# ② 2.1 视图一致性（图车道）
ssh 88zjwei "cd ~/camel-work/<树> && node test/tools/diff-gate/gate.mjs --gate view"

# ③ 2.3 改前改后无差异（前端车道与四种节点的读者）
ssh 88zjwei "cd ~/camel-work/<树> && node test/tools/diff-gate/gate.mjs --gate nodes"
```

`--gate 2.1` / `--gate 2.3` 是同样的两个门。基线、两边 install 的路径、并发位、两个语料切片全在
`baseline.json`，**不是命令行参数**——两条车道因此不可能量到两个不同的基线。

## 那一行怎么读

```
[GATE 2.1 view]  baseline=2576db796 lane=32140cf82 cases=34 output_differ=0 graph_differ=0 differences=0 unmeasurable=0 verdict=GREEN
[GATE 2.3 nodes] baseline=2576db796 lane=32140cf82 cases=27 output_differ=0 graph_differ=0 differences=0 unmeasurable=0 verdict=GREEN
```

红的样子（同一条 `32140cf82` 上人为造的一次改动：`std::gir` 的端口节点多印一个 `style=bold`，
`__op_println__` 在行尾多写一个 `!`）：

```
[GATE 2.1 view]  ... cases=34 output_differ=1  graph_differ=19 differences=20 unmeasurable=0 verdict=RED
[GATE 2.3 nodes] ... cases=27 output_differ=3  graph_differ=14 differences=17 unmeasurable=0 verdict=RED
```

**两个读数都取过**：无改动时两条门都是 `differences=0`（exit 0），造了那次改动后分别 20 与 17
（exit 1）。红的时候 `[DIFF]` 行会跟着打到 stderr，逐条写清哪个 case、哪一条 stdout 行、哪一处图。
所以「绿」不是「没检查」——同一对命令在改动后确实变了。

## 两条门各读什么、不读什么

| 门 | 条目 | 语料切片 | 读的是 |
|---|---|---|---|
| `view` | 2.1 视图一致性 | `smoke/core`、`src/compile/core`、`src/compile/gir`、`src/compile/graph_read`、`src/compile/branch_exit`、`src/compile/fold_arms`（34 个 case） | 同一批程序在改前改后**答出同一份输出、画同一张图**。编码器对每张图问的那次「冻结图上与草稿上两个读面是否一致」在这个读数里面：两个读面不一致的图会被拒，那个 case 的退出码就与基线不同，于是计进 `output_differ`。 |
| `nodes` | 2.3 改前改后无差异 | `src/compile/casts`、`src/compile/conversions`、`src/compile/array_alias`、`src/compile/broadcast_product_gradient`（27 个 case） | 会建出 Cast/Copy/Fill/Accs 的程序，改前改后输出一致、图一致。 |

**不读的**：时间只报告不判决（`diff-compare` 的规则）；`not-pinned` 的文本不比较（用例自己声明那
不是承诺）；性能计划不在切片里（那是另一条读数，`perf/**`）。

## 拒绝（exit 2）的四条

- 基线 install 不在，或它的 `build-info.json` 不是 `baseline.json` 里那个提交，或是脏树构建的；
- 这条车道的 install 还没有（先跑第 ① 步）；
- `diff-compare` 自己 exit 2（配置或某一边不可用）；
- `measured=0`，或所有 case 都 `unmeasurable`。

## 基线是怎么来的（一次性，不是每车道一次）

`baseline.commit = 2576db796` 的 Release install 从一条已经建过它的检出拷一份出来，**放在
`~/camel-work/` 之外**（`/home/zjwei/camel-baseline/df2-base`）：那个根是闸门的工作根，一次运行会
删掉「本地工作树已经不在了」的检出，而基线 install 不是检出，不能被当成检出清掉。

```bash
cp -a ~/camel-work/<某个在 2576db796 上构建过的检出>/out/latest /home/zjwei/camel-baseline/df2-base
```

## 为什么不是整层

整层是 6,000+ 个 block × 两次运行，那是合并位/闸门的读数，不是一条车道跑得起的读数。切片固定在
`baseline.json` 里、对所有车道相同，**这才是两条车道的「绿」可以互相比的前提**。