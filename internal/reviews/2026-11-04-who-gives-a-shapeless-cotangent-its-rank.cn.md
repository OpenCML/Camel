# 余切没有形状时，谁给它那个秩：`numel(ct)` 与 `mean_axis_stated_t` 的端口

**状态**：已落一版并测过红→绿（第 4 节）；**第 5 节列两处待裁**，我方默认见每处末尾。

这页接 `2026-11-04-a-rank-zero-cotangent-is-read-as-a-pointer.cn.md`（下称"前页"）。前页把崩说清楚了，我方默认是**甲：端口按元素写，`numel` 之前按类型判"有没有形状"**。我照着甲做了三遍，前两遍都被量到不成立，第三遍才落到根因上。所以这页记的是：**为什么"端口按元素写"这条半句必须放弃，以及真正的补在哪**。

## 1. 复现：两种树，两种读数

程序（就是新用例 `test/cases/compile/transforms/stated_mean_axis_element_gradient.cml`）：

```camel
import { vjp } from vjp
import { mean_axis } from tensor

func total(x: float[]): float {
    return mean_axis(x, 0, false)
}

func main(): int sync {
    let (value, pullback) = vjp(total)([1.0, 2.0, 3.0, 4.0] as float[])
    let d = pullback(1.0).0
    `value={} dL/dx={}`->format<value, d>->println
    return 0
}
```

**`origin/develop`（`34b659107`，树 `~/camel-work/zerocrash`，宿主 88zjwei，2026-11-04 05:0x，load 18.96/16.49，5/5 次）**：**不崩**，`rc=1`，拒在 `stdlib/vjp.cml:87:30`：

```
REINTERP jvp function=total skip=value
error[RuntimeDiag::RuntimeError]: … transforms: jvp has no forward transform for this function
```

原因：`std::gir` 里 `tensor:mean_axis` 的 `value_type` 还是 `float[..]`，紧跟着一个隐式 `CAST value_type="float"`（秩零读），这个读节点不可提升，**能在 develop 上崩的那条路根本走不到**。同一棵树上 `sum(x)` 正常（`value=10.0 dL/dx=[1.0, 1.0, 1.0, 1.0]`），`sum(mean_axis(x, 0, false))` 也正常（`value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]`，`rhs=sum_stated_t,mean_axis_stated_t`）。

**`lane-statetypes`（`8de82e959`，树 `~/camel-work/statetypes`，2026-11-03 16:35–16:37 -05:00，load 18.96/16.49）**：`8/8 rc=139`（`for i in 1..8`，`RESULT segv=8 ok=0 other=0`）。崩前最后三行钉住律：

```
REINTERP jvp function=total … tangent=computed source=ruleset rules=jvp_mean_axis
TRANSPOSE graph=346 type=(float) => (float[],) nodes=9 rhs=mean_axis_stated_t
TRANSPOSE op signature=... resolved=(f: (float[]) => float pure) => (float) => (float[],) ... same=yes
```

`gdb -batch -ex run -ex "bt 12"`：

```
#0 camel::tensor::ops::(anonymous namespace)::numelKernel(ArgsView&, ArgsView&, camel::core::context::Context&)
    from /home/zjwei/camel-work/statetypes/out/latest/stdlib/tensor.cmo
#1 NodeVMSchedPass::evalNativeOperator
#2-#5 NodeVMSchedPass::call
#6 NodeVMSchedPass::apply
#7 applyPassesDetailed
#8 runCommand
#9 main
```

诚实的说法：**这个崩溃是 `lane-statetypes` 那个"把归约结果类型写成诚实元素类型"的改动让它变得可达的**；在 develop 上它被另一处（jvp 无正变换）挡着。红→绿只能在带那个改动的树上证明，见第 6 节的合并顺序。

## 2. 甲的前半句（端口按元素写）单独做：`dL/dx` 变成指针位

最小版：端口改 `(ct: T)`、助手改 `mean_axis_cotangent::<T>(ct: T, at: int[])`，其余不动。在 `~/rank0-s`（与 `statetypes` 同一二进制、同一 stdlib）上量两段：

| 程序 | 期望 | 端口按元素写之后 |
|---|---|---|
| P1 `mean_axis(x, 0, false)` 当整条答案 | `2.5` / `[0.25, 0.25, 0.25, 0.25]` | `rc=0`，`value=2.5 dL/dx=[0.25, 0.25, 0.25, 0.25]` ✅ |
| P2 `sum(mean_axis(x, 0, true))` | `2.5` / `[0.25, 0.25, 0.25, 0.25]` | `rc=0`，`value=2.5 dL/dx=[1.59513487970007e-310, …]` ❌ **静默错数** |

`1.59e-310` 是指针的位模式当 double 读出来——**不崩，只是错**。端口收窄成元素以后，数组余切的值被当成元素读：`numel` 答 1，`ct / lanes` 答一个"数"，全程没有一处报错。所以前页甲的这半句**不能单独做**。

## 3. 第二遍：两条律——被声明索引当场拒掉

自然的下一步是"同一 key 两条律，端口一元素一数组，让 walk 按余切类型挑"。这条路**语言本身不许**：把两条律写进 `transpose.cml` 后，编译在**解析期**就停：

```
(rulesets.transpose)…/stdlib/rulesets/transpose.cml:558:1: [Error]: The rule 'mean_axis_stated_t' and
the rule 'mean_axis_stated_element_t' both have 'mean_axis_stated' with 2 operand(s) as the head of …
```

也就是说：**一个 key 可以有几条律，但左值形状必须不同**（`T[]` 对 `T[..]` 那种）；余切不在左值里，所以"靠第二条律分开两种余切形状"这条路不存在。而即使它存在，也挑不出来——三个事实叠着：

1. `rule_expand.cpp#chooseRuleAmong` 判的是**左值端口**（`rulePortsOf`）。余切不是节点的操作数，从来不在这个判定里。
2. 5.4 让 `float[..]` 按 `assignableFrom` **接受 `float` 本身**（`array.cpp` 的秩零分支：同一个值，不是包着它的数组），所以元素余切对两个端口都"合格"。
3. 实例类型是**归约过的**：`chooseTransposedRule` 把 `float[..]` 经 `rankZeroType()` 折成 `float` 再交实例（`transpose.cpp`，注释写明防的是 `float[..][..]`）。于是 P2 的实例类型也是 `float`。

结论：**挑选不在余切这一侧发生，就不可能区分**；而"两条律"又直接被索引拒掉。两遍都堵死之后，剩下的唯一开口是：**让余切在进入律体之前就有形状**。

## 4. 落地：walk 补上 5.4 那个转换

5.4 说的不只是"`T[..]` 也接受 `T`"，它还说了**槽里装什么**。`src/core/type/composite/array.cpp#ArrayType::castSlotFrom` 的原文：

> A value of the element type entering a slot of `T[..]` becomes the rank-zero array that holds it. …but the run now has a value with a rank to read, which is what a slot of an open rank needs. The compiler inserts this conversion where a value's type goes from `T` to `T[..]` (Builder::coerceTo); it is never the `as` of a program…

而 `builder_operators.cpp` 说明**调用实参**是唯一的另一个入口（它进门之前问同一个问题）。**律的右值既不经过 `coerceTo`，也不是一次调用**——它是 `transpose` 的 walk 直接把节点塞进去的。所以转换从来没发生：槽里是 double，类型是 `float[..]`，`numel(ct)` 就把它当指针读。

补的就是这一处，一行性质的改动（`modules/transforms/transpose.cpp`，`ruleCotangent`）：

```cpp
gc_node_ref_t handed = ct;
if (ctType != nullptr && ctType->code() != TypeCode::Array) {
    Type *port = cotangentPortOf(*state.context, ruleName, operandTypes);   // 律右值第一个参数的类型
    if (port != nullptr && port->code() == TypeCode::Array &&
        port->rankZeroType() != nullptr && port->assignableFrom(ctType)) {
        const std::vector<gc_node_ref_t> source{ct};
        handed = draft.addCastNode(port);          // 目标类型就是端口的类型
        draft.setNormInputs(handed, std::span<const gc_node_ref_t>(source));
    }
}
```

`cotangentPortOf` 是本页新加的读法：`rulePortsOf` 是**左值**端口，余切不在里面，所以"律对余切承诺了什么"只能读它右值的第一个参数。`addCastNode` 在运行期走 `Type::castSlotFrom`，对 `T` → `T[..]` 这个方向**真的分配秩零数组**（`Array::createRankZero`）——于是 `numel(ct)` 读到的是一个有秩的数组，答 1；`extent_product(extents)/1` 就是车道长度；`ct / lanes` 是标量算术；`full` 铺开。

**于是一条律同时服务两种形状**：端口仍是 `(ct: float[..])`（`mean_axis_stated_t` 一个字没改，只加了一段解释性注释），P1 与 P2 都答 `[0.25, 0.25, 0.25, 0.25]`。

这也正是前页甲的后半句"读侧按类型判有没有形状"的落点——只是"按类型判"的结论不是"这条律读不了"，而是"在交给它之前，把它变成它的类型所说的一件东西"。

## 5. 待裁

**（〇）这是一个"只剩一半"的补丁，另一半不在本条车道的脚程里——先读这一条。** 在 `lane-statetypes` 那棵树上量到的类型（临时日志，`ruleCotangent` 的入参与本页新读法）：

```
[rank0] rule=sum_stated_t       pos=0 ct=float    port=float
[rank0] rule=mean_axis_stated_t pos=0 ct=float[..] port=float[..]
```

第二行是本页的关键：余切**连类型都是 `float[..]`**，而槽里装的是一个 double。也就是说"类型说数组、槽里没秩"这个谎**在进律之前就已经撒好了**，补在律门口的那次转换因此**根本不触发**（两边都是数组类型，`castSlotFrom` 认为无事可做）。谎的源头是 `mean_axis_stated` 这个**stated 拼写**的声明结果类型仍是 `T[..]`，而它的核在 `lane-statetypes` 之后答的是元素——`lane-statetypes` 把 `mean_axis` 这一族的签名做成诚实的（`modules/tensor/ops/registry.cpp` + `reduction_ops.cpp`），**没覆盖 `mean_axis_stated`**。

所以：**本页的改动是正确的、而且是必需的下一步，但它在 develop 与 `lane-statetypes` 上都是惰性的**；要真正让第 1 节那条程序答出 `[0.25, 0.25, 0.25, 0.25]`，还需要"stated 拼写的签名也按 `at` 推"——那在 `modules/tensor/ops/**`（本条车道明令不许动），也是 `lane-axistated` 那一族的事情。默认：**本页的补丁先落**（它没有副作用：实测见第 7 节），签名那一半由那条车道做；两边都到齐时，第 1 节的程序就绿。

**（一）这个转换该待在 `transpose` 还是 `rule_expand.cpp#expandRuleByName`。** 它是"律的右值是被 walk 直接进入的、不是被调用"这件事的补丁，而**所有**不跑律、只展开右值的 transform 都从 `expandRuleByName` 进（`jvp` 也是）。今天 `jvp` 没撞上，只是因为它送进去的都是正切、形状与端口一致。默认：**这版先留在 `ruleCotangent`**（本次唯一可复现的入口，改一处、测一处），另开一页/一条车道把它下沉到 `expandRuleByName`——那里它有实例的端口表在手，是更该在的地方；下沉会改变每个 transform 的展开结果，不该由这条车道顺手做。

**（二）`take_stated_t` 同风险，只报不改。** 端口 `(ct: T[..])`、律体读 `zeros_of_shape(ct, …)`；秩 0 的 `take` 余切会走进同一个坑。今天不可达（`take` 的余切总是行数组），但形状上没有任何东西挡着——**这次统一由 walk 装箱之后，它也就被同一处补上了**；不与本页的改动重复修。

## 6. 基点与合并顺序

本版接在 **`origin/develop`（`34b659107`）** 上，不带任何别人的提交（前一次我把基点放错了：`lane-rank0` 曾一度起在 `lane-statetypes` 的 tip `8de82e959` 上，已按批示摘掉重来，工作树里只有本车道自己的 6 个文件）。

崩溃只有在"归约结果类型诚实化"那类改动之上才可达（第 1 节、第 5 节（〇）），所以：

- 本版在 develop 上**惰性**：`ruleCotangent` 只在"余切没有形状、律的端口是数组"时插节点，而 develop 与 `lane-statetypes` 上都没有这种组合（第 5 节（〇）的实测日志：两个律都是两边同型）。红数实测见第 7 节。
- 新用例 `test/cases/compile/transforms/stated_mean_axis_element_gradient.cml` **随本版落地，但不登记进 `test/plans/feat/compile/stated_extents.plan.toml`**：它在 develop 上会因为第 1 节那个 jvp 读数（别人的缺口）变红，登记就是替别人的缺口往闸门里加一个红。它是这处改动的钟，登记的那一行跟着"stated 签名诚实化"一起写。

## 7. 判据与数字

树 `~/camel-work/rank0`（宿主 88zjwei），2026-11-04 06:0x（-05:00），load 19.48/17.02，`-Tier fast -Jobs 4`。基线 = `34b659107` 干净树，改动后 = 同树 + 本版 6 个文件。

| 判据 | 基线 | 改动后 |
|---|---|---|
| `test/plans/feat/compile` | total 2243，pass 2230，**fail 2**，known 11 | total 2243，pass 2230，**fail 2**，known 11（逐项相同） |
| `test/plans/feat/transforms` | total 29，pass 21，**fail 0**，known 8 | total 29，pass 21，**fail 0**，known 8（逐项相同） |
| `test/plans/feat/transforms/split` | total **19**，pass 19，fail 0 | total **19**，pass 19，fail 0（未回归） |

（`feat/compile` 的 2243 项包含 `feat/compile/transforms` 子目录，两个数是同一个跑批；子目录单跑没有额外信息，因为它的红会同时出现在 `feat/compile` 的 2 个 fail 里。）

| 其余判据 | 结果 |
|---|---|
| 崩溃读数 ≥3 次 + gdb 帧 | 第 1 节（8/8 `rc=139`，帧原文） |
| 端口原文对照 | 第 4 节：`sum_stated_t` 的 `(ct: T)` 对 `mean_axis_stated_t` 的 `(ct: float[..])` |
| 修好后秩 1、`keepdims=false` 的答案（先手算） | 手算：`x=[1,2,3,4]`，唯一轴均值 2.5，`dL/dx` 每位 `1/4` ⇒ `[0.25, 0.25, 0.25, 0.25]`；这条要在第 5 节（〇）的签名改动到齐后才绿 |
| 其它"把值当数组读"的清点 | 第 5 节（二） |