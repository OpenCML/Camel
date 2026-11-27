# task-383 step 3 — `rulesets` 里没有 append 的规则，逆向本来就不读那个数组

**读数来源**：`lane-kinds-oper`，2026-11-28，`mm-capacity` 树、枝 `lane/loop-tape-values`，`tip e37e964cb3`，源码零改动。

## 一、写域扩大之后的第一步读数就把前提推翻了

```
$ ls src/stdlib/rulesets/
int8.cml  jvp.cml  probe_*.cml  transpose.cml  vmap.cml        （六个文件，无 append）
$ rg -n 'append' src/stdlib/rulesets/
（零命中）
```

`append` 的逆向是 **C++ 内建算子**，在 `src/stdlib/std/builtin/operators/struct.cpp:1490-1520`：

```
:1490-1492   // `tangent/init_arr(dy)` 与 `tangent/last_arr(dy)`：append 的逆向需要的两半
             //   y = append(xs, v) ⇒ dy 的前 n-1 个给 xs，最后一个给 v
:1493-1507   __tangent_init_arr__：只读 dy（cotangent 数组），不读 primal xs
:1510-1520   __tangent_last_arr__：只读 dy
```

⇒ **append 的逆向本来就不读那个累积数组**，它只需要 `dy`。所以"让逆向只读长度"这个前提不成立，`rulesets` 里也没有该改的规则。

## 二、那么 `float[]` 残差是谁要的

`[t383p]` 的四条数（前一步读数，已复核）：

```
[t383p] existingCollections=1 r=7 tapeSlots=7 existingSlots=1 states=3
[t383p] state    n=3: float[] int float
[t383p] residual n=7: float[] float float[] float float (fn) (fn)
[t383p] tape     n=7: float[][] float[] float[][] float[] float[] (fn)[] (fn)[]
[t383p] existing n=1: float[]
```

`residual[0]`/`residual[2]` 是 `float[]`、tape 槽是 `float[][]`（参差）。既然不是 append 的逆向读的，就是 **pullback body 里别的东西**在要 `acc` 的 primal 值。

## 三、下一格只需要一行读数

在 `pullback.cpp:5249` 之后，对每条 `residual` 打印**它在 pullback body 里被谁消费（oper id）**，然后按结果二选一：

- **(a) 消费者只要长度**（`len_arr` / `idx_arr` 一类）⇒ 在 body 里插一个 `:op/len_arr(residual)`，tape 记 `int`。**改动小、语义等价**。
- **(b) 消费者把整个数组当数组用**（`scatter` / `concat` 的原值等）⇒ **不能改成长度**；本卡的结论改成"这个形状的 tape 在 ONNX 侧无解"，**回来重画边界**（例如：这类程序导出时点名拒绝并给可判定的诊断，而不是当缺陷修）。

## 四、边界（照旧）

- 不改语义凑绿；(a) 的"等价"必须由消费者读数支撑，不是靠测试变绿推断。
- 探针与 `#include <cstdio>` 提交前还原；`git status` 回 0。
- 不对非 Oper 节点取 `normInputsOf` / `withInputsOf`（那是 `task-383` 前一格 `0xC0000005` 的真因）。
- **读数必须来自同一次"构建成功 + 运行"的 binary**（`BUILD_EXIT=0` 与 `binary built` 同一次运行）。
- 依赖 `616f65b436`（`lane/cc-loop-forward`，`pullback.cpp` 的 `argNameAt` 位置名）：`git cherry-pick -n 616f65b436` 借底子，跑完 `git checkout --` 还原。
- 不新开枝名；用 `lane/loop-tape-values`。
- 函数残差 `residual[5]`/`[6]`（tape 槽是函数数组，ONNX 同样不可表达）**不在本卡**，只是"另一条活的料"。

## 五、本卡为什么停在这

前一格连续三轮"构建 + 读源码"，余量到极限；再往下就是盲改 append / pullback，而它上面三次都拒绝了这个赌法。**停得对**：现在的状态是"三份读数在手 + 一行读数就能把路分成两条"，交给下一格接着打。
## 六、step 4 读数（结论：**(b)**，本卡结论改成"这个形状的 tape 在 ONNX 侧无解"）

树 `D:\Projects\Camel-wt\mm-capacity`，枝 `lane/loop-tape-values @ d3be48080e`（= `origin/integration`），
`git cherry-pick -n 616f65b436` 借底子；观测对象 = `task-383-shape-a-probe.md` 的 A 全文。
同一次运行：`BUILD_EXIT=0`、`[provenance] tip (dirty: 3 files) | binary built 2026-11-28T05:20:02+08:00`。
探针（`pullback.cpp`，buildLoopBody 里 `setRegionResult(bodyRegion, bodyResult)` 之后）：对每条
`residual` 取 `draft.normUsersOf`/`withUsersOf`，打消费节点的 oper/kind/type。原文：

```
[t383p] residual[0] type=float[] users=#15(:struct/append_arr[22],ty=float[]) #42(-,ty=(…大元组…))
[t383p] residual[1] type=float   users=#18(:op/add_d[158],ty=float) #15(:struct/append_arr[22],ty=float[]) #20(-,ty=(float[], int, float, float)) #42(-,…)
[t383p] residual[2] type=float[] users=#20(-,ty=(float[], int, float, float)) #42(-,…)
[t383p] residual[3] type=float   users=#18(:op/add_d[158],ty=float) #42(-,…)
[t383p] residual[4] type=float   users=#20(-,…) #42(-,…)
[t383p] residual[5] type=(float) => (float[4], float[4]) pure       users=#42(-,…)
[t383p] residual[6] type=(float[4]) => (float[4, 4],) pure          users=#42(-,…)
```

判读：

- **`residual[0]`（`float[]`，累积数组）的消费者就是 `:struct/append_arr[22]` 本身** —— 即反向 body 每轮
  都对上一轮的数组做一次 append（把这一轮的贡献收进去）。这不是"只要长度"的用法：append 的**第一个
  操作数是整个数组**。⇒ 分支 **(b)**：不能改成长度。
- `residual[2]`（另一个 `float[]`）没有算子消费者，只被 `#20`（GATE）与 `#42`（聚合）带过去 ⇒ 它本身
  可以被丢/保留，**但 `residual[0]` 决定了这条路走不通**。
- `residual[5]/[6]` 是函数值，只有聚合消费（另一条活的料，本卡不管）。
- `#20`/`#42` 是 GATE/聚合（非 Oper），这也是为什么"对非 Oper 取 `normInputsOf`"会假崩。

⇒ 结论：`float[]` 这一路的 tape 在 ONNX 侧**无解**（ONNX 的 Loop/Scan 不提供"对携带数组做 append"的
算子，append 的逆向虽然不读该数组，但**正向 body 自己每轮都读它**）。判据②（导出成功 + ORT/nvm 逐值）
在本卡范围内**不可达**；边界建议：导出对这个形状**点名拒绝**并给可判定诊断（"the reverse pass of a
collecting loop appends into an array, which ONNX has no operator for"），而不是落到通用的
"operator ':struct/append_arr' has no ONNX lowering …"。
`pullback.cpp:2478` 仍未动；源码零改动，`git status` 回 0。
## 七、step 5 读数（①）与类型层（②）

### ① append 在循环体的哪一路

树 `mm-capacity` @ `lane/loop-tape-values`，借 `616f65b436`，形状 A，同一次运行 `BUILD_EXIT=0` / `binary built 06:13:53`。探针在 `evalLoopNode`：打出 `components[]` 的 uri 与 `eval` 顺序。原文：

```
[t383c] component[0] uri=:struct/append_arr
[t383c] component[1] uri=:op/add_l
[t383c] component[2] uri=:op/add_d
[t383c] component[3] uri=:op/mul_d
[t383c] component[4] uri=
[t383c] component[5] uri=:op/mul_d
[t383c] component[6] uri=:struct/append_arr
[t383c] component[7] uri=
[t383c] component[8] uri=:op/add_d
[t383c] component[9] uri=
[t383c] component[10] uri=
[t383c] eval carried state j=1 (component[1])
[t383c] eval carried state j=2 (component[2])
[t383c] eval scan c=0 (component[3])
[t383c] eval scan c=1 (component[4])
[t383c] eval scan c=2 (component[5])
[t383c] eval scan c=3 (component[6])          <- 在这里被拒
```

判读（三条都是读数，不是推理）：

1. append 是 **scan 槽 3 的分量**（`components[6]`），不是"携带 state 的下一值"——
   `components[0..2]` 是三个 state，`components[3..10]` 是 8 个 scan 槽。
2. `components[6] == components[0]`：**同一节点既当 state 0 的分量、又当 scan 槽 3 的分量**（与
   `068a4b6596` 卡上"一个节点服务两个分量"的整数读数一致）。
3. state 0 已被 `stateIsTheCollection` 认领，认领它的槽是 **scan 槽 0**（append 的 `with` 操作数
   = `components[3]` = `:op/mul_d` ⇒ `with.front() == components[states+0]`），所以携带的是 state 1、2。

⇒ 落点因此不是"携带张量 state"，而是**"被认领的 collection 与该 collection 的 append 同时出现在
scan 槽里"**：槽 0 收元素（`mul_d`，stack 出数组），槽 3 收"数组本身"（每轮长度不同 ⇒ 参差）。
要补的降级必须让**数组走 sequence 携带**（`SequenceEmpty` / `SequenceInsert` / `ConcatFromSequence`），
而不是给 scan 槽 3 一条恒等/直通降级（那会丢收集语义）。

### ② 类型层（已提交 `4e8bff4312`）

`ValueInfo` 增 `isSequence`；`encodeValueInfo` 在该标志下把元素自己的 `TypeProto` 包进
`TypeProto.sequence_type`（field 4）。**只改写盘**，没有任何 caller 置位，未置位时字节与以前完全一致。

验收（最小 `.onnx`，输入 `seq(tensor(float))`、一个 `SequenceLength`、opset 11；同一 binary
`BUILD_EXIT=0` / `binary built 06:35:02`）：
```
export rc=0
opset=11 producer=camel
input type bytes: 22040a020801      # field4 len4 { field1 len2 { field1 varint 1 = FLOAT } }
onnx.checker ok (sequence type, opset 11)
PASS
```
`numstat`：`14 1 src/stdlib/onnx/proto/onnx_writer.cpp`、`6 0 src/stdlib/onnx/proto/onnx_writer.h`。
相邻计划 `feat/modules/onnx`：10 红，**全部落在纯净尖端的 11 红集合内**（`resnetbn_train_step_export`
这次因负载没超时，绿了）⇒ 新增红 0。

**两笔在枝上**：`28256c0a86`（点名拒绝，`13 0 exporter.cpp`）、`4e8bff4312`（类型层，2 文件）。
树 `git status` 空；探针与 `cherry-pick -n 616f65b436` 已还原。

### 一条要报给卡主的事实

`export_loop_carried_array_field` 这条 known-fail 的期望是
"the dtype or shape of a value changes from one call of a recursion to the next"，
现在那条用例**先被 append 的点名拒绝接住**（`known failure changed its mind` 提示）。不是新红
（它本来就在红集里），但那条 known-fail 的期望需要它的属主更新。
## 八、step 6：③（五个算子读数）与 ④ 的第一步，以及一个必须改设计的地方

### ③ 五个算子一个模型（`onnx.checker` + ORT 双口径）
```
export rc=0
opset=11 nodes=['SequenceEmpty','SequenceInsert','SequenceLength','SequenceAt','ConcatFromSequence']
input xs: 220c0a0a0a08080112040a020804      # seq(tensor(float)[4])，field4→field1→field1 三层
onnx.checker ok (five sequence operators, opset 11)
SequenceLength -> 2
SequenceInsert+SequenceAt -> [7. 8. 9. 10.]
SequenceInsert+ConcatFromSequence -> [7. 8. 9. 10.]
PASS
```
**双口径救回来的一个真错**：上一版把元素的 `TypeProto.Tensor` 直接塞进 `Sequence.elem_type`（只包一层），字节
`22040a020801`：**checker 接受**，**ORT 拒绝**（`Unsuported type proto value case`）。现在包两层
（`22060a040a020801`），ORT 读得过。已提交 `3884ccb0d8`（`10 4`）。

### ④ 第一步（已提交 `a765ee3973`）
`Value::Form` 加 `Sequence` + `symbolicSequence` 工厂 + `valueInfoOf(name, const Value&)`（按值取
`isSequence`，旧的事实接口默认 false ⇒ 不产生任何字节变化）；一处既有调用改用按值接口。
读数：`BUILD_EXIT=0`、`binary built 2026-11-28T07:11:37+08:00`、`feat/trans/macro` **30/30 pass**。
`numstat`：`13 3 exporter.cpp`、`21 1 value.h`。

### 第四步真正卡住的不是形状门，而是 `stateIsTheCollection` 的前提
`stateIsTheCollection` 的既有注释写着：被认领的 state"唯一用途就是那个 append，而那个 append 就是它自己的下一个值，
所以这一轮不评估它"（`exporter.cpp:2911-2916` 把它的 body 端口绑成 `Value{}`，`2930-2934` 跳过它）。
形状 A 的 ① 读数表明**这个前提不成立**：同一个 append 节点**又是 scan 槽 3 的分量**（`components[6] == components[0]`），
于是它**会被评估**，而它读的正是那个被绑成 `Value{}` 的端口 —— 两条路径打架。

⇒ 接线只有一条自洽的路（也就是决策者说的"用序列携带"）：
1. 被认领的 collection **不再走 scan**，改成**一个携带的 sequence state**：init `SequenceEmpty`，
   body 每轮对该 state `SequenceInsert(state, elem)`（elem 就是 scan 槽 0 那个元素），
   Loop 之后用 `ConcatFromSequence(axis=0)` 得到消费者要的那个数组；
2. scan 槽 0（元素堆叠）保持不变；**scan 槽 3 取消**（它的值由 sequence 承担，否则参差无法 stack）；
3. `stateIsTheCollection` 的认领要与 1 一致：认领条件里加"该 append 同时不是 scan 分量"，
   并对"是 scan 分量"的情形走 1 的新路 —— **同一数组只走一条路**（这条断言要写进代码注释）。
未开始改（时间到）。

### 待办（按 `D-1128-52` / `D-1128-57`）
- `export_loop_carried_array_field` 正向化（desc/verify）；
- `ort_loop_values_dynamic`、`ort_loop_loaded_weights` 的 `known_fail_reason` 指向 `task-383` 的缺口，
  `known_fail_msg` 照实际输出，加 `known_fail_ends_when`；
- 三条都要在降级落地后一起改（不先改成正向等降级）。
## 九、step 7：Loop 能携带序列（`07e61574fe`）+ 前缀读数（决定形状）

### 已提交 `07e61574fe`（`26 8 exporter.cpp`）
`isCarriable` 接受 `Sequence`；`placeholderLike` 保形；被携带序列的 body 输出**不套 `Identity`**（opset 11 无 sequence 型 schema），
直接用长出序列的那个节点的名字；state 输入/输出的 `ValueInfo` 都写 `sequence`。读数：`BUILD_EXIT=0`、
`binary built 2026-11-28T07:18:23+08:00`、`feat/trans/macro` 30/30、**零 graph 变化**（还没有任何地方造 sequence 值）。

### 前缀读数（形状 A，借 `616f65b436`，同一次 `BUILD_EXIT=0`）
探针在 `evalLoopNode` 的 components 上打每个 `:struct/append_arr` 的 norm 来源：
```
export rc=1
[t383q] loop body component[0] :struct/append_arr states=3
[t383q]   norm[0] <- body state param 1 (the round's state entry)
[t383q] loop body component[6] :struct/append_arr states=3
[t383q]   norm[0] <- body state param 1 (the round's state entry)
```
**结论（读数，不是推理）**：append 的 `norm` 绑的是**当轮的携带 state 入口** ⇒ 它读的是**前缀**（这一轮之前的数组）。
component[0] 与 component[6] 是同一个节点（与 ① 的 `components[6] == components[0]` 一致），所以两行读数相同。

⇒ 走的形状是"**前缀**"那一种：
- 被认领的 collection 改成**携带的 sequence state**：init `SequenceEmpty`，body 每轮 `SequenceInsert(S, elem)`，
  `S` 的当轮值就是前缀；
- 需要把前缀**当数组读**的地方（append 的 `norm`，以及任何 elementwise 读它的 Oper），在消费端做一次
  **`ConcatFromSequence(S, axis=0)`** 的强制转换 —— 这是 `SequenceInsert` 的逆操作，是**同一条规则**；
- scan 槽 3（值参差）取消；正向/逆向两侧是**同一变换**：两侧的 append 都从自己的携带 state 前缀读数据，
  所以两侧的 collection 都变成 sequence state，代码只有一条路（不是特例）。

### 未做（时间到）
`SequenceInsert` 接进 append 的降级、`ConcatFromSequence` 的消费端强制转换、`SequenceEmpty` 的 init、
取 `stateIsTheCollection` 的认领一致性（"同一数组两条路 ⇒ `unsupported`"）。三条 known-fail 与正向化
按 `D-1128-57` 排在降级能过之后。