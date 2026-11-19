# `std::cxx::inline` 在 MLP / transformer 上被 `PassEffectOrder` 拒：归类

**构建**：`1e6cfcb80` Release（`1e6cfcb803`，源指纹 `0244fca0cbac`，与 `2d7c5c3a1` 同源），宿主 `88zjwei`。
**两份档案**（原文）：

```
error[RuntimeDiag::PassEffectOrder]: 'std::cxx::inline' committed a graph whose conflicting effects are not
ordered: node 86 of graph 'main' (:struct/copy_arr), in one region of graph 'main', has no order against
node 140 of graph 'main' (fs:read_bytes), and at least one of the two writes (write unknown / read fs):
no data edge, no sync edge, no order edge and no region's GATE input makes one wait for the other …
   --> .../out/latest/stdlib/fs.cml:27:18
```

transformer 上同一条，只是节点号换成 `node 81 … (copy_arr)` 与 `node 95 … (fs:read_bytes)`。

## ① `:struct/copy_arr` 声明了什么（原文）

`camel inspect --ops showcase/c2-mlp-mnist/train.cml`：

```json
{"module":"<builtin>","name":"__copy_arr__","uri":":struct/copy_arr","with":0,"norm":1,
 "purity":"effectful","signature":"(arr: T[]) => T[]",
 "accesses":[{"class":"unknown","name":"global","binding":"ambient","mode":"write"}],
 "accesses_declared":false,"external":true,"may_fault":true}
```

- **`accesses_declared: false`** —— 它**没有声明**任何访问；列出来的那条是**落回读法**：
  `src/core/operator_semantics.cpp:288-296`，没有 `resources` 时
  `return {globalAccess(AccessMode::Write)}`，注释原话
  "No list: the operator writes the global resource, which every effect conflicts with"。
- 于是检查看到的是 **write（class `unknown`、name `global`、ambient）**，并且算子被标成 **`external: true`**。
- 它实际做的事（`src/builtin/operators/struct.cpp:844-854`）：`copyWithRoom(arr, 0, arrType)` ——
  **分配一份新存储**再拷进去，不写任何程序能命名的资源。注释说它是"loop body 只 append 时给循环状态的一份私有副本"。

⇒ **声明的资源与读写方式：没有声明；读法把它当成"写 global"（unknown/ambient），并当它是 external。**

## ② 原程序（inline 前）两个节点之间有没有路径

没有**它们之间**的边，但有确定先后：

- mlp：`:struct/copy_arr` 在 **`g1`**（`n60`），`fs:read_bytes` 在 **`g7`**（`n1`，`load_npy` 的图）；
  transformer：`g31`（`n46`）与 **`g64`**（`n1`）。**两个节点不在同一个图**，所以两条文档里它们之间没有边。
- 先后由**调用点**给出：被调体的 body 跑在 `g1` 里那个调用节点的位置上，而 `copy_arr` 是 `g1` 语句链上的一个语句
  ⇒ 程序里"读文件"与"拷数组"是有先后的，只是这条先后不是一条节点间的边。

## ③ inline 后断在哪

- 两处被调体拼进 `main` 的**同一个 region**（消息原文 "in one region of graph 'main'"）。
- 原来承载次序的**调用节点被消除**，拼进来的 `fs:read_bytes` 与调用者区域里的 `:struct/copy_arr`
  之间**没有任何 data / sync / order 边，也没有 region 的 GATE 输入**（消息原文）。断点就在**内联拼接处**。

## 归类

- **(ii) 成立，且是根因**：`copy_arr` 没有声明访问（`accesses_declared:false`），检查因此把它读成"写 global"，
  而这个算子只分配新存储（`copyWithRoom`）——**两个节点不共享任何资源**，检查拒的是一个不存在的冲突。
- **(i) 也成立在机制层面**：`inline` 把被调体拼进调用者区域后，没有给拼进来的 effect 与调用者的 effect 排上序；
  按**当前（错误的）声明**，这条序正是检查要求的。
- **(iii) 不成立**：原程序里两者有确定先后（调用点），不是"两边本来都没先后"。

**⇒ 介于 (ii) 与 (i)：根因 (ii)，触发 (i)。** 只修一处的话应修 (ii)（给 `:struct/copy_arr` 声明"只分配、不访问"），
那样这条拒不再出现；只修 (i) 也能让它通过，但会把一条本不必要的序写进图里，且别的把 effect 拼进区域的遍要各自再排一次。
**char-transformer 上同一对（`copy_arr` 81 / `read_bytes` 95）、同一归类。**

## 量不到的

- **修 (ii) 之后 inline 是否通过**：要给算子加声明 = 改源码，本项禁令不许；所以这条只有判断、没有读数。
- **(i) 若真需排序**的对照（给 `copy_arr` 一个"确实写某个资源"的变体）：同样要改源码，做不到。

## 附：`examples/case-study/README.md` 第 2 条按裁定改写

原说法（"同一个图、只是编号不同；今天编号跟着书写次序 ⇒ 9.5 的 red"）与 `item3b` §2 的结论不符，已按作者裁定改写为：
两处被交换的是两个**可能出故障**的 `tensor:reshape`，交换改变了**先失败的是哪一个**（原则 3.4：可能出故障的不可自由安排），
所以两份**本来就不该相同**；`camel inspect --ties` 报 **0 组**，没有可修的并列。
**只改了那一条的说法**：63 节点 / 93 边、`n4`/`n6`、字节数与 sha 都未动，其他段落未动。