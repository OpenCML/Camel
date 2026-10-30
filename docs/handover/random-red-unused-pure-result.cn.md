# 现象：同一个程序随机编译失败（`SemanticDiag::UnusedPureResult`），约 10 次 1–2 次

> **已定性**：不是随机，是**指针键悬空**——`bodyValueStatement_` 以裸指针做键、循环的 cond/body
> 臂从不 `erase`，`ArmBuild` 一死键就悬空，后建臂复用同一地址时把死体的最后一句当成「值没人用」。
> 最小复现见 `lane-flake`。本文保留的是**现象数据**：次数、两个位置、时间戳、二进制修订，
> 用来给那个最小复现做旁证——**两个位置不同，正是「悬空键被不同地址复用」的指纹**。

与任何车道无关；单独报，方便派人查。**这是今晚第一个「随机红」，比任何功能都该先定性。**

## 一句话

`examples/models/naive-in-expert-out/train_step.cml` 用同一台机器、同一个二进制、同一条命令行
（只做编译 + `std::gir`）反复跑，**大多数成功，少数以 exit 1 失败**，错误是同一条
`SemanticDiag::UnusedPureResult`（「`>=` 的结果没被使用」），但**报错指向的位置每次不同**，
且指到的那一行看起来与 `>=` 无关。

## 复现记录

机器：本机（Windows），二进制：develop `out/latest/bin/camel.exe`（stamp 与 develop 一致，
`node scripts/build-info.js check` 通过），未改任何源码。

命令：

```
camel examples/models/naive-in-expert-out/train_step.cml std::gir
```

第 1 组（连跑 20 次，时间戳为每次启动时刻，2026-10-30）：

| run | 时刻 | exit |
|----:|------|-----:|
| 1–7 | 14:12:27.041 … .539 | 0 |
| **8** | **14:12:27.616** | **1** |
| 9–17 | 14:12:27.708 … .390 | 0 |
| **18** | **14:12:28.472** | **1** |
| 19–20 | 14:12:28.548 … .629 | 0 |

第 2 组（再连跑 40 次，遇红即停）：第 **12** 次红，`14:12:34.015`，exit 1，**stderr 为空**
（诊断走 stdout）。

第 3 组（再连跑 40 次，遇红即停）：第 **8** 次红，`14:12:43.157`，exit 1。

合计 100 次里 5 次红（约 5%，与之前一次 10 次 1 红一致量级）。

## 数据摘要（给最小复现做旁证）

| 项 | 值 |
|---|---|
| 样本 | 20 + 40 + 40 = 100 次，5 次红（5%） |
| 日期 | 2026-10-30，14:12:27 … 14:12:43 |
| 命中的 run | 第 1 组 run 8 / run 18；第 2 组 run 12；第 3 组 run 8；再加更早 1 次 |
| 观察到的两个位置 | `131:58`（报 `>=`）、`169:30`（报 `+`，`for b in m.blocks do n + count_block(b)`） |
| 二进制 | 本机 `out/latest`（develop 构建，`camel.exe`），未改任何源码；平台 Windows |
| 命令行 | `camel examples/models/naive-in-expert-out/train_step.cml std::gir` |
| 失败的 stderr | 空（诊断走 stdout） |

**为什么两个位置是关键**：同一份源码、同一条诊断，报错却落在两个互不相干的节点上。这不是
「某个源码形态有问题」，而是**诊断绑定的键在两次运行里指向了不同的内存**——与「裸指针做键、
死臂不 `erase`、后建臂复用地址」的指纹一致。

## 失败时的完整输出（第 3 组，原样）

```
error[SemanticDiag::UnusedPureResult]: The result of '>=' is not used, and '>=' changes nothing: it returns a new value; use the result ('let ys = ...') or drop it on purpose with 'let _ = ...'.
    --> D:\Projects\Camel-wt\h-defun\examples\models\naive-in-expert-out\train_step.cml:131:58
     |
 131 | }
     |  ^
help: The operations of the standard library return new values and leave their arguments as they were: after 'let ys = xs->append<5>', 'xs' is unchanged. To change a variable, bind it to the result, 'xs = xs->append<5>', as 'x = x + 1' does for a number.

camel: 1 error
```

## 为什么值得先查（现已定性，这里是当时的推理）

1. **位置是错的**：`131:58` 落在函数结尾的 `}` 上，`>=` 不可能在那里；说明报错节点被赋了一个
   不相干的 origin（诊断 origin 串了），或者检查跑在了一个只建了一半的节点上。
   —— 现在知道：悬空键指向的那块内存被复用后，取到的是**别的臂的最后一句**。
2. **每次红的节点不同**：更早一次观察红在 `:169:30`，报的是 `+`（`for b in m.blocks do n +
   count_block(b)`）；这次红在 `131:58`，报的是 `>=`。**同一条诊断、不同的节点**，像是检查遍历
   时拿到了共享/未初始化状态，而不是某个确定的源码形态有问题。
   —— 现在知道：不同地址被复用，就落在不同的死体上，所以位置每次不同。
3. **随机性影响闸门**：它在 3,359 份语料的两遍 `std::gir` 比对里制造了一次「1 → 0」的退出码
   变化（`defun-gir-baseline.tsv` 与 `-2.tsv` 的唯一非行序差异）。任何按内容比对语料的闸门都会
   被它随机打断。

## 我排除的

- 不是 `std::gir` 特有：只做编译、不跑 pass 时同样会红（上面命令就是 `std::gir`）。
- 不是磁盘/环境：同一秒内连续多次成功，失败与成功交替。
- 不是那条源码真的有问题：若真有问题，每次都必须红。