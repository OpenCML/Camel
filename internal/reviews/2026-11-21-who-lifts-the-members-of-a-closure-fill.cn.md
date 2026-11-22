# 闭包构造 Fill 的成员，在配对提升里由谁提升

**日期**：2026-11-21 17:20（+08:00）
**来源**：`jvp-closure` 车道第 2 轮，按作者第 10 条改掉「该读 `liftedFrom()` 而没读」那一处之后，`jvp_vjp_f` **不再报 `norm-arity mismatch`**，改在下一步 **SIGSEGV**。停手报出。

## 一、上一轮那个槽里的 380 是谁写进去的（作者第 ① 条，已查明）

**不是任何一处「写槽」写进去的，是运行期算子造出来的。**

`fillFrameForIndirectCall` 里那个 `Function*`（graphId=380）由 `transforms:jvp` 算子的 kernel 在运行期创建：

- 位置：`modules/transforms/jvp_operator.cpp:184`，`jvpKernel` 里
  `const JvpGraph built = buildJvpGraph(ctx.shared_from_this(), function->program(), function->graph(), readings());`
- 它收到的 `function->graph()` 就是 **365 `jvp_of___g_362__`**（`liftedFrom=362`），不是 364，也不是 362。所以「`liftFuncValue` 收到 364 还是 380」这个问法不成立：**364 从来没进过这个槽**，槽里先是被调用图 364 的**返回**闭包占着，而那个闭包就是算子对 365 再提升一次得到的 380。

四处算子调用的实测（`kernel=…` 逐行，同一程序一次运行）：

| kernel | operand | id | liftedFrom | staticType | resultType | operand graph funcType |
|---|---|---|---|---|---|---|
| jvp | `__g_359__` | 359 | 0 | `(x: f32) => f32` | `(f32,f32)=>(f32,f32)` | `(x: f32) => f32` |
| liftPairedClosure | `__g_362__` | 362 | 0 | — | — | — |
| jvp | `sq` | 275 | 0 | `(x: f32) => f32 pure` | `(f32,f32)=>(f32,f32) pure` | `(x: f32) => f32 pure` |
| **jvp** | **`jvp_of___g_362__`** | **365** | **362** | `(xs_0: f32) => (f32, (ct: f32) => (f32,))` | **`(f32,f32)=>((f32,f32), rule)`** | **`(f32,f32)=>((f32,f32), rule)`** |

最后一行是本轮的关键读数，两句都成立：

1. **操作数的静态类型是未提升的 `F`**（闭包自己的签名），所以写这个 Oper 节点的一侧按 `Lift<F>` 定了它的结果类型 —— 节点本身是**自洽的**；
2. **操作数背后那张图（365）的签名，逐字等于这次调用的 `resultType`** —— 也就是说**这个值已经就是这次调用要答的那一个提升**。

于是 `jvpKernel` 无条件 `buildJvpGraph` ⇒ 造出 `Lift<Lift<F>>`（380，4 个 norm 端口），而调用点按 `Lift<F>` 只送 2 个实参 ⇒ `Runtime indirect call norm-arity mismatch`。

**为什么构建期修不掉**：`liftedValueOf`（`jvp.cpp:1090-1102`）已经有一条同形状的判据（「副本的类型 == 这个类型提升后的类型 ⇒ 就返回副本」），它在这个失败上**不触发** —— 因为该值的**静态类型**是 `F`，而它**已经是**提升，这件事只写在图上（`GCGraph::liftedFrom`），不写在类型里。作者第 10 条说的「`liftFuncValue` 手里就有 `GCGraph*`」对**调用点**成立，但这个失败不在调用点：它是**算子运行期**对一个值再提升一次。

## 二、已按第 ② 条改的那一处（不是调用点）

`modules/transforms/jvp_operator.cpp`，`jvpKernel` 开头新加一个短路，两个读数同时成立才生效：

- `operand->liftedFrom() != kInvalidGraphId`（这个值背后是一张提升图；同一文件 `liftTransposeKernel` 的 424 行本来就这么读它自己的操作数），**且**
- `operand->funcType()->equals(norm.resultType())`（这张图的签名**就是**这次调用解析出来要答的签名）。

成立就把操作数原样答回去（`toSlot<::Function *>(function)`），不造第二张图。`jvp∘jvp` 不受影响：那里操作数的静态类型已经是 `Jvp<F>`，`resultType` 是 `Jvp<Jvp<F>>`，与操作数图签名不等，照旧提升。

## 三、改完的实测

| 项 | 读数 |
|---|---|
| 原报错 | **消失**（不再有 `norm-arity mismatch`）；`jvp_of___g_359__` node 5 的槽从 380 变成 **365** |
| `jvp_vjp_f` | **仍然不过**：`SIGSEGV`（nvm、fvm 都是 139），见下一节 |
| `smoke` | `total 10 / pass 10 / fail 0` |
| `gpu-plans` | `total 146 / pass 133 / fail 13`，13 个名字与记录**逐名不变** |
| `feat/transforms + feat/trans + feat/linear` | `total 1049 / pass 1018 / fail 0 / skip 2 / known 29` |
| `walk.cpp` / `walk.h` / `walkmachinery.cpp` | 对 `origin/develop` **0 行 diff** |

## 四、新的墙：一个闭包构造 Fill 的成员被写成裸标量

栈（gdb，Release 无符号，函数名可读）：

```
#0 rtdata::isViewValue
#1 rtdata::assertStoredValueIsNotView
#2 execute::writeRuntimeFillSlots
#3 NodeVMSchedPass::call
```

崩溃那一处 Fill 的读数（在所有 `jvp_of_*` 图的 Fill 上打点，最后一条就是它）：

```
graph='jvp_of_split_of_jvp_of_sq_forward' node=6 type='(f32) => f32 pure' members=1 [0]=0x3f800000
```

`0x3f800000` 是 **1.0f**（本 case 的 `v`）。也就是说：一个**闭包类型**（`(f32) => f32 pure`）的 Fill，把它的捕获槽写成了一个**裸 f32**，而运行期按该闭包的 `tupleType()` 认为这个槽是 **GC-traced** 的（`(f32,f32)` 这一对），于是 `assertStoredValueIsNotView` 把 `0x3f800000` 当对象指针解引用 ⇒ segv。

来源可指到一处（**未改，按作者「报那一处是谁」的口径停手**）：

- `jvp.cpp:1773-1802`，Fill 臂的 `liftsByComponent` 判据里写着
  `fillShape.type != nullptr && !fillShape.pair && node->dataType()->code() == TypeCode::Tuple`，
  注释（1769-1772）解释闭包 Fill **不走**按成员提升的路：「一个函数值的提升不是它成员的 fill，而是 `jvp(v)`」。
- 于是闭包 Fill 的成员一律取 `valueOf[member] / copyOf[member]`（**未提升的**那一份），而同一张图在 `pairedCaptures` 下的关闭口被 `liftGraph` 声明成**成对**的（本车道 `3fc9bb01d` 第 1 条）。

**两处对同一件事各说一句，谁都没错，错在层**：声明捕获布局的那一处按「这一层算出的值要带切向」说成对，而写成员的那一处按「闭包 Fill 不按成员提升」说成裸值。

## 五、要裁的那一件事

**`pairedCaptures` 下，闭包构造 Fill 的成员该由谁提升？** 三种答法：

1. **Fill 臂在 `pairedCaptures` 时对闭包成员也调 `liftedValueOf`**（只多一个条件，不动类型、不动别的 pass）。与 tuple Fill 已有行为一致，改动面最小。
2. **`liftGraph` 不要替闭包声明成对布局**（配对只发生在 tuple Fill 上），即把「这一层算出的值带切向」限定在按成员提升的那些 Fill 上。
3. **让 `split` 那一趟自己决定**（上一页 `2026-11-21-who-pairs-the-captures-of-a-body-built-closure.cn.md` 的 (a) 路）—— 但这一处 Fill 是 `split_of_jvp_of_sq_forward` 被提升时由 `jvp.cpp` 造的，`split` 已经不在了。

## 默认建议（写死，下一轮按它开工）

**走 1，并且只改这一处**：

> 在 `jvp.cpp` 的 Fill 臂里，当 `pairedCaptures` 为真且该 Fill 的目标类型是**函数**（闭包构造）时，成员取 `liftedValueOf(member, memberType)` 而不是 `valueOf/copyOf`；`liftsByComponent` 的判据与 `fillType` 一律不动。

判据（与上一页同一套口径）：
- `jvp_vjp_f` → 打印 `1.0 / 2.0 / 3.0`，nvm 与 fvm 都过；
- `smoke 10/10/0`、`gpu-plans 146/133/13` 13 名逐名不变、`walk.cpp/walk.h/walkmachinery.cpp` 一个字节不动；
- `feat/transforms + feat/trans + feat/linear` 的 `1049 / 1018 / 0 / 2 / 29` 不变。

**若走 1 之后 `jvp_of_split_of_jvp_of_sq_forward` 改报别的错（尤其是 `transpose` 的拒绝词）⇒ 那是本页之外的第二件事，停手另报。**

## 六、顺带：`known_fail` 那条标记怎么处理

`test/plans/feat/transforms/compositions.plan.toml` 的 `compositions.jvp_vjp_f` 现在只有 `known_fail = ["nvm","fvm"]` 而**没有 `known_fail_msg`**，所以任何失败都算预期 —— 这正是这个失败整夜隐形的原因（它在 `feat` 层，不在 `smoke`）。

**处理口径（下一轮落地）**：
1. 只要这一条还红，就把 `known_fail_msg` 钉在**当天那一句**上：本轮是
   `expected exit 0, got exit 1`（`SIGSEGV` 时是 exit 139）。钉子一钉，红在别处就报红。
2. `jvp_vjp_f` 一旦通过，就**整条删掉** `known_fail*`（不留 `known_fail_ends_when` 的空壳），让它在闸门里真正开始计分。
3. 不许用 `known_fail` 代替 `smoke`：这一条要继续留在 `feat` 层，但通过之后应加进 `smoke` 的一条组合用例，否则它回到「只有跑 `feat` 才看得见」。

## 决策者答（2026-11-23，据现行设计原则与核心抽象复核）

已由决策者 11-21 的四次答复解决（12:10、13:20、13:35、13:50），负责人 11-22 写进核心抽象第 6 节：根的捕获切向为零，体内造的闭包捕获 `(x, dx)`；"已经提升过"只在一趟变换之内有意义，由本趟映射承担："任何函数类型的值，只要它的提升值能从映射里取到……经由它的调用就直接调那份提升值"；跨调用的复用按（变换、源函数、版本、变体）作为事实缓存。 本页问的正是 13:20 答复第 1、3 条，答案是选项 1（成员取本层的 `(r, dr)`）。页里 `jvpKernel` 按 `liftedFrom` 做的短路是过渡做法，骨架里删掉。
