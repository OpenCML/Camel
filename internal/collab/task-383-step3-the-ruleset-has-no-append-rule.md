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