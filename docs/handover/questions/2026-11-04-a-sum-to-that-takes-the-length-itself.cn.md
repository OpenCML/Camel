# 由长度直接收长度的 `sum_to`：`sum_to_at(t, at)`

**状态**：待裁，且**本轮不做**（作者 2026-11-04 04:20 原话："更干净的是做一个直接收 `at` 的算子，排在后面"）。本页只把它写成一张可执行 TODO，把它需要裁的那一句问清楚，并把今天的替代读法钉住。

## 1. 今天树上的读法（本轮实测）

`stdlib/rulesets/transpose.cml#sum_to_stated::<T>(t: T[..], at: int[]): T[..] = sum_to(t, zeros::<T>(at))`：`sum_to` 的第二个操作数按文档**只被读形状**，所以律用规则层交来的长度自己造一串零当形状模板。作者 04:20 对这条读法的裁定是"用已知长度造一个形状模板，不顶替任何参数的值，规则上可以接受"。

探针（`D:\Temp\zc_step2.cml`，宿主 `88zjwei`，2026-11-03 23:5x `-05:00`，1 m load 24.3）：

```
source shape= [2, 3, 4]  numel= 24      // t: float[..]，类型不陈述任何 extents
template shape= [3, 4]  numel= 12       // zeros::<float>([3, 4])，长度是运行时的 int[]
reduced shape= [3, 4]  numel= 12        // sum_to(t, template)
from-type shape= [3, 4]  reduced= [3, 4]  // zeros::<float>(extent_of::<float[3, 4]>())
```

即：**长度已知、形状开放时，`sum_to` 的第二个操作数确实拿到了正确的目标形状**，逗号前后两种写法（运行时的 `int[]`、类型的 `extent_of`）都通。所以本轮不改 `sum_to`。

## 2. 要裁的那一句

`zeros::<T>(at)` 每次调用都要**造一个零数组**，而它被读的只有 `shape`：一处多余的分配，一处读者要绕一圈才看明白的写法（"为什么这里有一个零？"）。`sum_to_at(t: T[..], at: int[]): T[..]` 把这件事说白：第二个参数就是目标形状，不是另一个同类值。

要裁的是它的**语义边界**，因为 `sum_to(t, like)` 今天允许的归约（把 `t` 的形状按广播规则收到 `like` 的形状）与"目标形状就是一个 `int[]`"之间存在一处必须写死的规则：

- 甲：`sum_to_at` **只收**"`at` 是 `t` 的某个后缀"（即只做前导轴归约），越界的 `at` 由内核按今天的广播规则拒绝。与 `sum_to` 语义完全一致，只是一句"这个操作数就是形状"。
- 乙：`sum_to_at` 允许任意 `at`（`t` 可以被广播或重排到 `at`），于是它与 `reshape`/`sum` 的组合有重叠，得再裁一次谁是谁。

## 3. 我方默认

**甲，且排在孪生算子（`mul_stated`/`matmul_stated`）之后。** 理由：孪生算子关掉的是**红的五条**（见 `2026-11-04-who-carries-the-shape-as-an-operand.cn.md`），`sum_to_at` 关掉的是**一处多余的分配和一处绕弯的写法**，没有任何用例因为缺它而红。同日作者对"更干净"的说法也正是"排在后面"。

落地时的清单（照 `sum_stated` 的位置）：`modules/tensor/ops/**` 新增 OpDef（`name = "sum_to_at"`，三处导出、`infer` 读 `at` 的长度、内核转调今天的 `sum_to`）、注册表、`stdlib/tensor.cml` 声明、`stdlib/signatures/tensor.cml` 与 `docs/stdlib-reference.md` 由仓库脚本重生成、`test/plans/feat/modules/stdlib_registry` 的算子表现更新、`stdlib/rulesets/transpose.cml#sum_to_stated` 改成 `sum_to_at(t, at)` 并删掉那一串零。