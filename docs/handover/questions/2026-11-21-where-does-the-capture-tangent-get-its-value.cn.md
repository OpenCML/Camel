# 捕获的切向从哪里拿到它的值

**日期**：2026-11-21 11:50（+08:00）
**来源**：`jvp-closure` 车道按 `2e1887ae9` §2.4 的第一步实测，与 §2.2 的前提矛盾，回来问。

## 一、量到的（不是推的）

今天提升一个函数值时，每个捕获成为被提升图的一个**闭包端口，值不变**；它的**切向是该类型的零**，没有第二个「切向端口」。依据：

1. `modules/transforms/jvp.cpp:1472-1491`：捕获的 copy 是那个端口，切向 `zeroConstantFor(...)` / `zeroOfValueAtRunTime(...)`；注释逐字：`its tangent is the zero of its own type and there is no second port to hand a direction in`。
2. `jvp.cpp:872-917`：捕获 → `draft.addPortNode(captureType, ...)` + `appendClosureNode`，类型取源闭包的**原样**。
3. `modules/transforms/jvp_operator.cpp:219-226`：`jvpKernel` 建 Func 后把 `f` 的闭包**逐字复制**——闭包布局就是 `f` 的布局，没有切向位。
4. `jvp.cpp:2099-2131`（Call）：穿过捕获的函数值的调用写成 `Call(jvp(v))`，捕获的函数值只在运行期提升一次。
5. 读数（`jvp_vjp_f`，`--trace transforms`）：`REINTERP jvp function=split_of_jvp_of_sq_backward graph=381 liftedFrom=378 type=(f32, f32) => (f32, f32)`，随后 `TRANSPOSE … skip=call-value-missing param=2 node=4 arg=0` —— 今天的次序确实是「先提升 `lin`，再转置提升结果」。

## 二、为什么这不是「等你发话才做」的事

`design-principles.cn.md` 9.9 与 `nested-transforms.cn.md` §3 都写着：**被内层捕获的外层值，在内层看来是普通输入、切向量为零**。所以零切向不是疏漏。

于是第一步真正的难点不是「补一个切向端口」（那会打穿 `floatgate.f32_closure_member_ports`），而是**区分「外层递进来的切向」与「这一层自己的种子」** —— §3 的「不混淆扰动」就靠这条。

## 三、两处落点

### 3.1 第一步的运行期一半不在授权改的文件里

提升图的闭包由 `jvp_operator.cpp#jvpKernel` 建（上面第 1 节第 3 条）。只改 `jvp.cpp` 无法让 2n 布局被填上，会打穿 `floatgate.f32_closure_member_ports` 一类读数。

### 3.2 第二步的墙仍在

提升图里**拿不到 `lin` 的原值**（`Lift<(y, lin)>` 里 `lin` 已是 `jvp(lin)`）。所以「提升转置成员」有两条路：

- **路 (a)**：让 `split` 把原值一起交出来（改 `split.cpp`；`convergence-n` 正在拆它 ⇒ 冲突面大）
- **路 (b)**：让提升在**提升时自建被提升的被调图**（`buildJvpGraph` 递归 + 带 `(x, dx)` 闭包的 Func 节点，全在 `jvp.cpp`）

## 四、为什么必须问（而不是我自己定）

你的原话（`2e1887ae9`）：**「实现前先量今天的提升对捕获做了什么，结论与 §2.2 不一致就带读数来问。」** 现在结论与 §2.2 不一致，所以按你的指令带读数来问。

**而 §2.2 的偏差本身要说明**：它说「今天 GNC 在这里返回 Unsupported」，但 `git grep '类型会变\|type-changing' modules/transforms` **为空**，捕获端口自 `10200dc269`（2026-11-03）就在，**早于 11-18 的设计稿**。§2.4 的说法（只按参数提升，这是缺口）与读数一致。

## 默认建议（写死，本轮按它开工）

**① 准改 `jvp_operator.cpp`**：它不被四条待合分支（`split-transpose-cpp` / `convergence-n` / `convergence-o` / `ratchet-fell`）触碰，而第一步的运行期一半只能在它那里落。只改它的**闭包布局那一处**，不动别的。

**② 第二步走路 (b)**：全在 `jvp.cpp`，不与正在拆 `split.cpp` 的 `convergence-n` 争同一批行。判据：路 (b) 完成后 `jvp_vjp_f` 打印 `1.0 / 2.0 / 3.0`、`grad(grad(x⁴))(2) = 48`；若路 (b) 做不到这两条而路 (a) 能，**允许改路**，但要报出「为什么 (b) 不够」的读数。

**③ 若第一步的「区分外层切向与本层种子」需要新的表示（例如给捕获端口加一个显式的来源标记），而那表示会影响 `floatgate.*` 三条读数 ⇒ 停手报我，不自己加。** 那三条现在是 `floatgate.f32_closure_member_ports`、`floatgate.f32_array_zero_carrier`、`residuals_in_the_closure`，逐字不动是硬判据。