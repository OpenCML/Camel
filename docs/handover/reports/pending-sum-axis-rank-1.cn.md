# `sum<0>` 在秩 1 上的答案形状（已落，读数见下）

**状态：已落。** 主机恢复后取补丁、跑验收、提交（读数见第七节）。

## 一、根因，一句话

**规则写错了形状**：`std::cpp` 的 `tensor:sum_axis` 规则在秩 1 上写 `rt::sumAxis`（答数组），
而该节点的类型是元素本身（解释器答标量），C++ 编译器因此拒绝 ——
**修法是把秩 1 的答案写成 `rt::sumAll`（同为从 +0 升序累加，答元素），不是改用例、更不是改期望值。**

## 二、补丁是什么

| 项 | 值 |
|---|---|
| 存放 | `stash@{0}`，提交 **34a1e06d** |
| 消息 | `h-guard: sum_axis rank-1 writes sumAll (census does-not-build; unverified, host down ...)` |
| 规模 | `+25 / −7`，**一处**：`src/passes/trans/cpp/operator_rules.cpp`（`tensor:sum_axis` 规则） |
| 内容 | 秩 1 且轴为 `0`/`-1` 且 `keepDims` 是常量 `false` → 写 `rt::sumAll(xs)`；轴是操作数或标志是计算值 → 保留拒绝（那时规则也不知道答案的秩） |

**取用前先核对，不按序号乱取**（共享 stash 列表是踩过的坑）：

```powershell
git stash list                                  # 确认 34a1e06d 还在
git rev-parse stash@{0}                         # 必须是 34a1e06d
git stash pop 'stash@{0}'                       # 只在核对一致之后
```

## 三、验收命令原文 + 三个预期数

```powershell
pwsh -NoProfile -File D:\Projects\Camel-wt\camel-remote.ps1 -Path D:\Projects\Camel-wt\h-guard `
     -Plan test/plans/feat/trans/cpp.plan.toml -Jobs 8
```

主机上单条复现与全树普查：

```bash
python3 test/tools/cppexec/run.py test/cases/modules/tensor/int64_sum.cml   # 期望 1 same
./out/latest/bin/camel test/cases/modules/tensor/int64_sum.cml std::cpp     # 期望与 std::fvm 输出相同
```

| 期望读数 | 值 |
|---|---|
| 普查 `does not build` | **0**（补丁前在 fsh-dev 上 1；在本树本来就是 0，因为本树对该形状直接 `refused`） |
| `sum<0>` 那个程序的输出 | `9007199254740995`（`sum<0>(big)`，秩 1 求和 = 全部元素和，与解释器逐字相同）；整段输出见下 |
| 7 目录 `[SUMMARY]` | 七个目录各自一行 `[SUMMARY] total …`，全绿（红只允许 `gpu_preference.named_device` 这条机器状态） |

整段输出（补丁前在本树实测，`std::nvm`）：

```
9007199254740995
3002399800000000.0
9007199254740995
[-9223372036854775804, -4]
-9223372036854775808
[-4.611686e+18, -2.0]
```

## 四、跨树读数不一致（比补丁本身更值得记）

**同一条用例在两棵树上给两个答案**：

| 树 | 读数 | 原因 |
|---|---|---|
| `6af543d27`（原 `lane-guard`，该分支已删，本树） | `does not build 0` / `refused 1` | `tensor:sum_axis` 规则对 `rank < 2` **直接拒绝** |
| `fsh-dev` | `does not build 1`（`const int64_t v5_sum_axis = rt::sumAxis(...)`） | 那道秩 1 的拒绝被**删掉**，却仍写返回数组的 `rt::sumAxis` |

**判据**：「同一条用例在两棵树上一个是编不过、一个是被拒，说明两棵树对同一件事的判断不同——
先查是谁删了那道拒绝，再谈修。」

**要查的**：fsh-dev 上删掉 `rank < 2` 那一句的那笔（`operator_rules.cpp` 的注释写的是
“a rank of one is *not* refused … the open rank of the result's type writes that like any other answer”），
它以为结果是开放秩的数组，而节点类型实际是元素。**那一句的假设就是这次编不过的来源。**

## 五、期间不做的事

- 主机不可达期间**不起构建、不提交**（本页只有文档）。
- 不动 `stash@{0}`，不 `drop`。
- 不为了让用例编过而改期望值。

## 六、判据（进页）

「一条读数在两个地方不一样的时候，先问“哪一个是判据”，再问“谁把它改成了另一个”。」

## 七、落地的读数（主机恢复后实测）

```
camel /tmp/sum0.cml std::cpp | grep sum
  -> const int64_t v2_sum_axis = rt::sumAll(v1_reshape);
python3 test/tools/cppexec/run.py /tmp/sum0.cml
  -> 1  100.0%  same; 1 of the 1 standalone
```

**补丁确实把形状写对了**：秩 1 的 `sum<0>` 现在写 `rt::sumAll`，生成的程序与解释器逐字相同，且不链运行时（standalone）。

七目录计划（`-Tier fast`，8 路）：

| 目录 | `[SUMMARY]` |
|---|---|
| `feat/modules` | total 1855, pass 1853, fail 1, skip 1（唯一红 `gpu_preference.named_device`，机器状态） |
| `feat/trans` | total 72, pass 72, fail 0 |
| `feat/compile` | total 1994, pass 1994, fail 0 |
| `feat/opt` | total 1091, pass 1091, fail 0 |
| `feat/linear` | total 873, pass 871, fail 0, known failures 2 |
| `feat/mm` | total 283, pass 283, fail 0 |
| `feat/parse` | total 109, pass 109, fail 0 |

`feat/trans/cpp`（本笔的直接闸门）：`total 29, pass 29, fail 0`。

### 一个必须写下的更正：本树普查的 `does not build` 是 7，不是 0

全树普查（`python3 test/tools/cppexec/run.py test/cases`）在本树给出 **7 条 `does not build`**：
`opt/generic` 4 条（`fold_constant_loop`、`fold_constant_loop_effect`、`interval_bounds_computed`、`interval_bounds_constant`）、
`parse/constructs` 2 条（`constas`、`constas_param`）、`trans/cml/interval` 1 条。**`int64_sum.cml` 不在其中**，它是 `refused`（卡在 `tensor:mean` 对 `int[3]`，那是另一页的缺口）。

原因不是本笔：**本分支落后 `origin/develop` 392 笔**，这几条在新树上的状态与这里不同；fsh-dev 页里那句「does not build 1」是它那棵树（已合并 develop）的读数。所以「1 → 0」这个验收**在本树量不出来**，能给出的是：①秩 1 的 `sum<0>` 生成了正确的 C++ 并与解释器相同（上面两条读数）；②本树普查里 `int64_sum.cml` 从来不是 `does not build`。

**判据**：「一条读数要连同它所在的那棵树、那个提交一起报——离开树的『1』和『7』都不是数。」

## 八、对齐 `origin/develop` 之后的三个数

`git merge --no-ff origin/develop`（不 rebase、不 force），一处冲突（本文件同族的 `operator_rules.cpp` 注释），按“两边都留”解完：`git grep -n '^<<<<<<< '` → **0**，`node scripts/diagnostics-catalog.mjs --check` → **exit 0**（305 codes 全有名字与消息）。合并提交 **75b9c4c5f**。

| 验收 | 对齐后的读数 |
|---|---|
| 全树 `does not build` | **0**（普查 3277 个程序：same 1255、not a program 1082、refused 930、differs 9、crashes 1、**does not build 0**）——**逐条列文件名：空** |
| `int64_sum.cml` | **`1 100.0% same`**，且 standalone；它不再 `refused`，也不再 `does not build` |
| 本笔是否还绿（`feat/trans/cpp`） | `[SUMMARY] total: 51, pass: 51, fail: 0` |

**替那句判据添一个例证**：旧树（落后 392 笔）的 7 条 `does not build`（`opt/generic` 4、`parse/constructs` 2、`trans/cml/interval` 1）**在对齐后的树上一条不剩**——它们不是缺口，是旧树上的红。

**还有一处必须记住的细化**：对齐后立刻发现 `test/cases/trans/cpp/sum_axis_rank.cml` 红——`const rt::Array<float> v7_sum_axis = rt::sumAll(...)` 编不过。原因是**答案的形状由节点类型决定，不由规则自己挑**：`sum<0>(xs)` 的答案类型是元素（印 `8.0`），`sum_axis(xs, 0, false)` 的签名是 `T[..]`、答案是秩 0 的数组（印 `8.0`、`shape` 是 `[]`）。所以规则现在**先读节点自己的类型**：元素 → `rt::sumAll`，数组 → `rt::sumAxis`。两棵树 51 条全绿就是这条的读数。

**判据（细化后的）**：「答案的形状由节点的类型说，不由写规则的人挑——挑错的那天，两个形式里总有一个编不过。」
