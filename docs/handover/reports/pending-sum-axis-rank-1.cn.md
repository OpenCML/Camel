# 待验的补丁：`sum<0>` 在秩 1 上的答案形状

**状态：补丁写好，未验证，未提交。** 主机 `88zjwei`（10.126.62.88）在本笔前后不可达——
ping 100% 丢包、22 端口超时、staging push `git exit 128`，两次间隔重试同样。按纪律
（每次提交前必须 `BUILD_EXIT=0` 与闸门），**不提交未验证的源码改动**。

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
| `lane-guard`（本树） | `does not build 0` / `refused 1` | `tensor:sum_axis` 规则对 `rank < 2` **直接拒绝** |
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