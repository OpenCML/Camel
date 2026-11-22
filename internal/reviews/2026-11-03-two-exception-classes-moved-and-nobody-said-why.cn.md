# `EXCEPTIONS` 的两个数字早就错了：补记账，还是二分到引入提交？

**问：`scripts/stdlib-reference.mjs` 的 `EXCEPTIONS` 里有两个数在 `375e726b8` 之前就已经和实测对不上，是**直接补记账（改数 + 说"类动了"）**，还是**二分到那笔引入提交**（改数 + 点名是哪个重载移了类）？默认建议：**二分，但要设上限**——按 §3 的路线走，超过 8 次构建就转成"补记账 + 在提交信息里写明区间"，不许无限期让 fast 层红着等。**

## 1. 读数（两棵树，同一份检查）

`develop`（`4e7033b00`）上 `registry.*` 13 条里 5 条红；`375e726b8` 那棵树上同一条计划是 **13 条里 4 条红**，红的正是这四条：

| 检查 | `375e726b8` | `4e7033b00` |
| --- | --- | --- |
| `registry.signatures_match` | **红** | 红 |
| `registry.result_types_match` | **红** | 红 |
| `registry.parameter_types_match` | **红** | 红 |
| `registry.migrations_stay_migrated` | **红** | 红 |
| `registry.docs_complete` | 绿 | **红**（新红，见另一页/另一条车道） |

两棵树上红的是**同一批**，且这两行原文在 `375e726b8` 上就一模一样：

```
EXCEPTIONS.result['declaration states a generic application'] records 2, and the run found 1: update the number in the same commit and say why the class moved
EXCEPTIONS.result['rule states a tuple'] records 6, and the run found 7: update the number in the same commit and say why the class moved
2 exception problem(s) between the run and EXCEPTIONS: a class it does not record, a number that moved, or an overload that carries no class
```

⇒ **这是老红，不是今晚带来的**。三条测试（`signatures_match` / `result_types_match` / `parameter_types_match`）红的是**同一个根因**：`--verify-ops` 在这两行上 `exit 1`，**来不及打印对账句**，所以三条各自报 `stdout does not contain 'the declared … compared'`。这不是三个问题，是一个。

顺带证伪一条："`:io/print` 的 pack 无类型"**不是**根因。它出现在参数那一节的例外清单里（`untyped pack ..rest: 6`，与记录值 6 相符），属于**被记录在案**的例外，本身不产生 problem。把它当根因会修错地方。

## 2. 为什么这个数必须"说清为什么动"

`EXCEPTIONS` 的头注释把规矩写死了：**"唯一通过的办法是给这一类命名并在这里改数，而且要在同一个提交里说明清单为什么变长或变短。"** 这两个字不是格式要求——它们是"这个检查自己认下的例外"的台账。改数不说原因，等于把"不知道"写进台账。

## 3. 三条路与代价

| 路 | 做法 | 代价 |
| --- | --- | --- |
| **甲（默认）** | 二分到引入提交：用 `--verify-ops` 的**例外清单**（每个重载逐条列名）对比，找出哪两个重载移了类，改数并点名 | 每次探针要一棵新树 + 一次全量构建（约 5 分钟）；起点 `375e726b8` 已红，终点未知，**可能很远**。所以设 8 次上限 |
| 乙 | 现在就改 `2 → 1`、`6 → 7`，提交信息写"类动了，引入提交早于 `375e726b8`" | 便宜，但把"不知道是哪个重载"写进台账，违反 §2 的规矩；下一次同类红没法用这次的记录对照 |
| 丙 | 不改，红着等 | fast 层一直红；且这 3 条会把真正的红淹掉 |

**默认选甲，上限 8 次构建**。到上限就转乙，并在提交信息里写明二分过的区间边界（最老一个仍红的提交 + 最新一个已绿的提交），这样下一个人能接着走。**任何情况下不许把它标成 `known_fail`**（本轮任务书明确禁止）。

## 4. 交接用的一条线索

`rule states a tuple` 从 6 涨到 7：结果侧整份例外清单里有 `split (transforms:split): declares (v: float, d: float) => (float, (float, float) => float)`、`attempt (:mark/attempt): declares F!`、`transpose`、`vmap`、`from_bytes ×5` 等 13 条具名项。`declaration states a generic application` 从 2 掉到 1：像是某个声明从 `Device<T>` 这类泛型应用改成了具名类型。

二分时**不必看整份 stdout**，只要 diff 两棵树的 `the declared result type was not compared for 29 operator(s)` 这一节的**具名清单**——移了类的重载会在两行之间换位置，`git grep` 出来的签名文本能直接对上提交。