# 训练基础设施设计

状态：设计稿，2026-10-18。语言作者已同意三项方向性裁定（参数树、无状态随机数、字节类型，见设计原则 2.16）与本稿的优先级；其余细节按本稿执行，标为"待定"的条目实现前再问。

依据：对 [OpenCML/Camel-DDPM](https://github.com/OpenCML/Camel-DDPM)（0533d89）的评审，以及写它的 AI 给出的反馈；Camel 版本为 `develop` 合入 `fsh-dev` 后的 be0854ab。DDPM 仓库附带的 Camel 补丁（`sqrt` 的梯度、二进制文件写出、批量矩阵乘、cuBLAS BF16 矩阵乘）已被 `develop` 吸收（96967307、d14707d6），本稿不再重复。

相关文档：[设计原则](../../design-principles.cn.md)（尤其 2.13 数值契约）、[Tiles 路线](../tiles-route/README.cn.md)、[多目标编译](../tiles-targets/README.md)、[算子语义](../op-semantics/README.cn.md)、[原生算子回调子图](../native-subgraph-call/README.md)、[自动微分作为图改写](../../technical/26_autodiff_as_graph_rewrite.md)。

---

## 1. 问题与证据

DDPM 项目已经能用 Camel 写完整的训练流程：模型、损失、`value_and_grad`、Adam、EMA、采样都在 Camel 里，GPU 上跑。暴露的问题集中在下面几处。

### 1.1 参数树只能靠代码生成

`tools/generate-model-params.py` 按 `tools/model_schema.py` 为每种参数结构生成一个模块。以前馈层为例（`src/params_ff.cml`，节选）：

```cml
export type FF = { up: Tensor<f32>, gate_w: Tensor<f32>, down: Tensor<f32> }
export type DeviceFF = { up: Device<Tensor<f32>>, gate_w: Device<Tensor<f32>>, down: Device<Tensor<f32>> }

export func uploadFF(p: FF): DeviceFF sync {
    return { up: upload(p.up), gate_w: upload(p.gate_w), down: upload(p.down) }
}
export func emaFF(p: FF, q: FF): FF {
    return { up: p.up * 0.999 + q.up * 0.001, gate_w: p.gate_w * 0.999 + q.gate_w * 0.001, down: p.down * 0.999 + q.down * 0.001 }
}
export func stepFF(p: FF, g: FF, m: FF, v: FF, rate: float, bc1: float, bc2: float): (FF, FF, FF) {
    let s_up = adam_leaf(p.up, g.up, m.up, v.up, rate, bc1, bc2)
    ...
}
```

每种结构 12 个函数（初始化、上传、下载、清零、加载、EMA、缩放、范数、距离、Adam……），外加一个镜像类型 `DeviceFF`。七种结构共生成十个模块。原因有两个：

- 值层面：没有"对结构的每个叶子做同一件事"的写法；
- 类型层面：没有办法从 `FF` 得到"每个字段都在设备上的 `FF`"。

而核心内部早已理解这种树：`value_and_grad(loss)<m>` 对整个 `Model` 求梯度并返回同构的梯度；`run_device` 按原结构返回设备值。缺的只是让用户也能写。

### 1.2 一个训练步在主机与设备之间往返

`src/train_gpu.cml` 的一步：

```cml
let clean = read_batch('data/train-images-idx3-ubyte', offset, batch)   // 主机读文件
let t = randint(0, len(s.beta) - 1)                                      // 主机随机数
let noise = randn([batch, 784])                                          // 主机随机数
let noisy = q_sample(clean, noise, s.abar[t])                            // 主机加噪
let vg = run_device<differentiate>((m, noisy, noise, ...))               // 上传、GPU 求梯度
let next = run_device<update>((m, vg.1, mu, nu, ...))                    // 第二次 GPU 调用
```

数据、随机数、加噪都在主机上，每步上传两个批量张量，前向反向与更新是两次独立的调用。潜变量扩散的入口更甚：潜变量下载到主机加噪再上传。根本原因是随机数只能在主机上生成：现有的 `randn`、`randint` 读写进程级的生成器状态，是外部效应，不能编进 GPU 内核。

### 1.3 其余

- 算子能否求导、能否上 GPU，只有写到一半才知道（`sqrt` 缺梯度，形状、逆置换、批量矩阵乘在 GPU 梯度路径上缺实现）。
- 混合精度只有一条 cuBLAS BF16 矩阵乘路径；激活、主权重、优化器仍是 f32，没有 bf16 张量类型。
- 没有重计算、梯度累积、显存峰值报告。
- 不能多卡训练。
- 二进制数据经 `string` 读写，曾因嵌入的 NUL 截断图片；检查点的原子发布、分段启动由 Python 完成，每段重启一次 Camel。
- 训练循环用带 12 个参数的尾递归写成；`loop … do` 语法已可用。

### 1.4 判断

这些问题里，参数树与设备上的随机数是语言与标准库层面的缺口，靠库代码绕不开；其余是标准库、自动微分与 Tiles 的衔接。多卡排在单卡一步完全留在设备上之后：往返的一步扩到四卡，浪费也翻四倍。

---

## 2. 原则

1. **训练是普通的 Camel 程序。** 不引入隐藏的框架或运行时会话对象；模型、优化器、数据管线都是可检查的图。
2. **语义不因设备而变。** CPU 上运行原图是参考语义；GPU、多卡是同一语义的物理实现，受 2.13 数值契约约束。
3. **放宽有边界。** 累加顺序默认可以改变（设计原则 2.15、2.16），但结果必须可复现；降低精度、近似数学、FMA 收缩、特殊值假设只在程序以 `@numeric(...)` 授予相应许可时使用。后端的安全模式按规范算法逐位计算。
4. **能从已有事实推出的，不再手写。** 树结构来自类型；算子的梯度与 GPU 实现能从定义图推出时自动推出；支持矩阵从注册表生成。
5. **随机性是值，不是状态。** 训练所需的随机数由显式的键决定，因而可以进入纯计算、跨设备一致、可存进检查点。

---

## 3. 参数树：`std::tree`

### 3.1 什么是树

- **树**：由结构体与元组嵌套而成的值。其余值都是**叶子**（张量、标量、`Device<…>`、字符串、键等）。
- 第一阶段数组、映射、`enum` 不算树的内部节点，整体作为叶子。数组长度在类型里不固定，展开要到运行时才能确定；以后若需要，单独设计（待定 T1）。
- 树的形状完全由类型决定，因此下面所有操作都在编译期展开成逐叶的普通节点：运行时没有额外开销，自动微分与 Tiles 看到的只是普通的图。

### 3.2 操作

新模块 `tree`（`modules/tree`，也可放进现有标准库模块，技术选择）导出：

| 操作 | 含义 | 类型规则 |
|---|---|---|
| `tree_map<f>(t1, …, tn)` | 对位取 n 棵树的叶子，作为 n 个实参调用 `f` | 叶子边界由 `t1` 的类型决定；`t2…tn` 的结构必须与 `t1` 相同，否则编译错误并指出第一个不同的路径。结果的结构同 `t1`，每个叶子换成 `f` 的返回值 |
| `tree_unzip(r, like)` | `r` 由 `tree_map` 得到、每个叶子位置是 k 元组时，拆成 k 棵树 | 叶子边界由 `like` 的类型决定；`r` 在 `like` 的每个叶子位置必须是同一元数 k 的元组。结果是 k 棵与 `like` 同构的树组成的元组 |
| `tree_reduce<op>(t, init)` | 按叶子的规范顺序（3.3）从左到右折叠 | `op: (A, L) => A` |
| `tree_leaves(t)` | 叶子按规范顺序组成的元组 | |
| `tree_paths(t)` | 每个叶子的路径字符串（`ff.up`、`blocks.0.att.q`），结构同 `t` | 用于序列化与诊断 |

**叶子边界永远由树实参的类型决定，从不由回调决定**（2026-10-18 补充）：

- `tree_map` 以第一棵树为准，在它的叶子处停止展开，其余树在同一位置的值整体作为实参，即使那里是元组或结构体。因此不存在"元组叶子"与普通元组相混的问题：多棵树的叶子不先组合成元组，而是直接作为多个实参传给 `f`。
- `tree_map` 的回调返回元组时，结果在类型上就是"含元组的树"，叶子边界已经丢失；`tree_unzip` 因此必须给出参照树 `like`，按它的叶子边界拆开。
- 不采用"按回调的形参类型决定停在哪一层"：`f` 是重载集合或泛型函数时，停止的层次会随重载选择改变，含义不稳定，错误也难以定位。
- 不采用显式包装（如 `Leaf<T>`）：第一阶段没有必须把结构体当作叶子的场景；以后若出现（例如把配置结构整体当作一个叶子），再单独设计。

`f` 必须能作用于每一组叶子类型；不能时报编译错误，列出第一个不适用的叶子的路径与类型。`f` 可以是重载集合或泛型函数，按每个叶子的类型分别选择。这些操作的类型由核心计算，与现有内建算子的类型由实参算出的方式相同，不需要新语法。

### 3.3 规范顺序

叶子顺序为深度优先、按字段声明顺序（元组按下标）。`tree_reduce`、`tree_leaves`、序列化都用这个顺序。浮点求和的结果依赖顺序，因此顺序是语义的一部分，不得由实现改变。

### 3.4 `Device<T>` 作用于树

`Device<T>` 对结构体与元组按字段分配：

```
Device<{ a: A, b: (B, C) }>  ≡  { a: Device<A>, b: (Device<B>, Device<C>) }
```

两边是同一个类型（类型构造时规范化），不是两个可以互相转换的类型。`upload`、`download` 接受树；`run_device` 的结果类型本来就是这个形式，现在有了名字。叶子必须是 `Device` 支持的类型（张量或 `float`），否则编译错误并指出路径。

一般的"映射类型"（对任意类型函数按字段分配）属于尚未决定的类型表达式语言（设计原则第 6 节），本稿只定 `Device`。

### 3.5 改写后的样子

`params_ff.cml` 里的 12 个函数与镜像类型全部不再需要。优化器与 EMA 对所有结构通用：

```cml
import { tree_map, tree_unzip, tree_reduce } from tree
import { Tensor, sum, sqrt } from tensor

func adam_leaf(p: Tensor<f32>, g: Tensor<f32>, m: Tensor<f32>, v: Tensor<f32>, h: Hyper): (Tensor<f32>, Tensor<f32>, Tensor<f32>) {
    let nm = m * 0.9 + g * 0.1
    let nv = v * 0.999 + g * g * 0.001
    return (p - h.rate * (nm / h.bc1) / (sqrt(nv / h.bc2) + 0.00000001), nm, nv)
}

export func adam(p: typeas P, g: P, m: P, v: P, h: Hyper): (P, P, P) {
    let r = tree_map<(a: Tensor<f32>, b: Tensor<f32>, c: Tensor<f32>, d: Tensor<f32>) => adam_leaf(a, b, c, d, h)>(p, g, m, v)
    return tree_unzip(r, p)       // 按 p 的叶子边界拆成 (新参数, 新 m, 新 v)
}
export func ema(avg: typeas P, p: P): P {
    return tree_map<(a: Tensor<f32>, b: Tensor<f32>): Tensor<f32> => a * 0.999 + b * 0.001>(avg, p)
}
export func sq_distance(p: typeas P, q: P): float {
    return tree_map<(a: Tensor<f32>, b: Tensor<f32>): float => sum((a - b) * (a - b))>(p, q)
        ->tree_reduce<(acc: float, x: float): float => acc + x>(0.0)
}
export func sq_norm(p: typeas P): float {
    return p->tree_reduce<(acc: float, x: Tensor<f32>): float => acc + sum(x * x)>(0.0)
}
```

`tree_unzip` 只是方便：上面的 Adam 也可以写成三次 `tree_map`（先算新的 m、v，再用它们算新参数），展开后的图相同。

参数的初始化仍逐层写（不同层的形状与初始化方式本来就不同），但只写一次值，不再写类型的镜像。

### 3.6 与自动微分、Tiles 的关系

- `value_and_grad` 已对结构参数返回同构梯度；树操作展开为逐叶节点后，梯度照常通过。
- `@gpu` 与 `run_device` 已按结构传递参数；`Device<P>` 规范化后，用户写的类型与 Tiles 返回的类型一致。
- 展开在类型确定之后、自动微分之前进行（技术选择，实现时与 `std::macro` 的位置一起确定）。

---

## 4. 整个训练步留在设备上

### 4.1 无状态随机数

新增计数器式随机数（与 JAX 同一思路），现有带全局状态的 `randn`、`randint`、`seed` 保持不变。

```cml
import { key, fold, split, normal, uniform, randint } from random

let k     = key(20261018)                 // 由种子得到的键
let ks    = fold(k, step)                 // 由键与整数派生新键
let (a, b) = split(k)                     // 一个键拆成两个互不相关的键
let noise = normal(ks, [batch, 784])      // 只由键与形状决定
let t     = randint(fold(ks, 1), 0, 999)
```

- **键**是不透明的值类型 `Key`（128 位），可打印、可比较、可存储，参与树操作（作为叶子）。
- 这些函数是**纯函数**：同一个键给出同样的数，不读写任何状态，不是外部效应。因此能与其余计算一起编进 GPU 内核，能出现在被重计算的区域里（第 7 节），能在多卡上各自生成。
- 用户负责不重复使用同一个键。重复使用不是错误（结果就是相同的数），以后可加提示（待定 T2）。

**算法（规范）**：Philox4x32-10。张量按行优先线性下标，第 i 个元素取计数器 i 的输出，与设备和并行划分无关。`uniform` 由 32 位输出按规定的位操作构造 `[0,1)` 的 f32（`(bits >> 8) * 2^-24`）；`randint` 用规定的无偏拒绝算法；`fold` 与 `split` 用规定的 Philox 派生。

**数值契约**：键派生、`uniform`、`randint` 只用整数运算与正确舍入运算，**跨设备逐位相同**。`normal` 用 Box–Muller，涉及 `log`、`sqrt`、`sin`、`cos`，按 2.13 只保证声明的 ulp 上界；实现应共用核心的同一套超越函数算法（核心的向量数学可原样编成 CUDA），使 CPU 与 Tiles 在实践中逐位一致，测试按 ulp 上界写。

### 4.2 数据上设备

- 能放进显存的数据集，一次 `upload` 整个张量；每步的批量用 `gather(data, idx)` 取，`idx` 由键生成。整步因此没有主机输入。
- 放不下的数据集用第 8.3 节的分片读取与预取，预取由运行时在后台完成，程序里只看到"下一批"。

### 4.3 一个函数完成一步

```cml
type State = { params: Params, m: Params, v: Params, ema: Params, key: Key, step: int }

@gpu
func train_step(s: Device<State>, data: Device<Tensor<f32>>, cfg: Config): (Device<State>, Device<float>) {
    let k     = fold(s.key, s.step)
    let idx   = randint(fold(k, 0), 0, cfg.count, [cfg.batch])
    let clean = gather(data, idx)
    let t     = randint(fold(k, 1), 0, cfg.timesteps - 1)
    let noise = normal(fold(k, 2), [cfg.batch, 784])
    let noisy = q_sample(clean, noise, cfg.abar[t])
    let (l, g) = value_and_grad(loss)<s.params>(noisy, noise, embedding(t))
    let (p, m, v) = adam(s.params, g, s.m, s.v, hyper(cfg, s.step))
    return ({ params: p, m: m, v: v, ema: ema(s.ema, p), key: s.key, step: s.step + 1 }, l)
}

func main(): int sync {
    ...
    let final = loop s = upload(init_state(cfg)) for i in range(0, cfg.steps) do sync {
        let (next, l) = train_step(s, data, cfg)
        if i % cfg.report == 0 then println('step={} loss={}'->format<i, download(l)>)
        return next
    }
    ...
}
```

`Params` 是模型参数的结构体类型；泛型函数用 `typeas P` 就地绑定类型（设计原则中的类型变量规则）。`Device<State>` 按 3.4 分配到每个叶子；`key`、`step` 这类叶子需要 Tiles 支持 `Device<Key>`、`Device<int>`，或把它们留在主机作为参数（技术选择）。Tiles 把 `train_step` 编成一个区域：状态常驻设备，每步只有在报告时才下载一个标量。

### 4.4 报告隐式传输与同步

Tiles 为每个 `@gpu` 调用点统计：上传与下载的字节数和次数、主机同步次数、因不支持而退回 CPU 的区域。

- `camel inspect --tiles-transfers`（名称由 Tiles 定）给出静态可知的部分；运行结束时可打印运行期统计。
- Tiles 可提供严格模式（模块导出的标注参数，不是语言关键字）：调用点出现未显式要求的传输即报错。用于确认训练步真正常驻。

这是 Tiles 的工作，见第 10 节分工。

---

## 5. 算子支持矩阵

### 5.1 生成而不是手写

`camel inspect --ops --support`（选项名为技术选择）从各注册表生成一张表，每个算子一行：

| 列 | 来源 |
|---|---|
| 稳定标识与版本、数值分类 | `OperatorTable::semantics` |
| dtype、广播、空张量 | 算子的类型规则与索引声明 |
| CPU 内核 | 算子表 |
| 定义图 | `definitionOf` |
| 反向（原生 / 由定义推出 / 无） | autodiff 的 VJP 注册表 |
| Tiles（原生 / 由定义推出 / 退回 CPU） | Tiles 的能力声明 |
| ONNX 导出 | 导出器 |

生成结果写入 `docs/` 的一个目录文件（与诊断目录 `docs/diagnostics.md` 同样的做法），CI 检查它与注册表一致。

### 5.2 由定义图推出

已有定义图的算子（softmax、log_softmax、mean、relu、sigmoid……，见算子语义第 13 节）：

- **反向**：没有原生 VJP 时，autodiff 对展开后的定义图求导。定义图是规范，所以推出的梯度按构造正确；性能可能不如手写，表中标为"推出"。
- **Tiles**：没有原生实现时，Tiles 降低展开后的定义图。

于是新算子只要写出定义图，就同时具备 CPU 参考、反向、GPU 实现；原生实现只是性能优化。

### 5.3 测试要求

- 每个可求导算子：f64 有限差分检查；原生与推出的反向都存在时互相比较。
- Tiles：正确舍入类运算与 CPU 逐位相同，超越函数按声明的 ulp 上界（Tiles 路线 10.2）。
- 新增算子必须有声明，以及定义图或原生反向之一，并附测试；支持矩阵不得倒退（CI 比较）。

---

## 6. 混合精度

### 6.1 存储类型

张量增加 `bf16` 与 `f16` 元素类型：`Tensor<bf16>`。转换显式写出：`x as Tensor<bf16>`（就近舍入，偶数优先）、`y as Tensor<f32>`（精确）。显式转换是程序可见的语义，不需要数值许可。

### 6.2 运算的规范语义

- **逐元素的加减乘除与开方**：对 bf16 操作数，结果是精确结果按 bf16 正确舍入一次。实现可以先在 f32 中正确舍入再舍入到 bf16：f32 的精度 24 位不小于 bf16 精度的两倍加二（2×8+2=18），这种二次舍入与一次舍入结果相同（已知定理），因此不需要许可。f16（11 位）同理（24 ≥ 24）。
- **矩阵乘与归约**：bf16 输入精确转为 f32，按 f32 的规范算法计算（矩阵乘为按 k 递增的 FMA 链，归约按规范顺序），结果最后舍入一次到输出类型。
- **Tensor Core 等硬件路径**改变累加顺序，按 2.16 默认允许（安全模式下不用）；把 f32 矩阵乘改在 bf16 或 TF32 中计算仍需要 `change_precision`。现有 cuBLAS BF16 路径要求 `reassociate`、`change_precision`、`finite_math` 三项，其中 `reassociate` 按 2.16 可以去掉。

### 6.3 训练中的用法

主权重与优化器状态保持 f32，前向用 bf16：

```cml
let half = s.params->tree_map<(x: Tensor<f32>): Tensor<bf16> => x as Tensor<bf16>>
```

自动微分中转换的反向是反向转换。f16 需要的损失缩放以后再做（bf16 不需要）。

---

## 7. 显存：分析、重计算与梯度累积

- **分析**：`camel inspect --memory`（技术选择）对给定入口与形状报告训练图的峰值活跃字节数、各阶段占用，以及前向为反向保存的张量。Tiles 报告设备上的实际峰值。用实测决定模型规模。
- **重计算**：标注 `@recompute` 写在函数或块上。自动微分对该区域只保存输入，反向时重新计算前向。由于区域是纯计算，重算结果与保存的结果逐位相同，语义不变。区域内用带状态的 `randn` 会改变结果，因此拒绝并提示改用第 4.1 节的键式随机数。标注由 autodiff 模块导出，不是语言关键字（与 `@numeric` 的处理一致，名称待定 T3）。
- **梯度累积**：库函数，对若干微批量依次求梯度并用 `tree_map` 对位相加，顺序固定，结果确定。它与单个大批量在浮点上不逐位相同（求和顺序不同），文档写明。

---

## 8. 数据、检查点与长时间运行

### 8.1 字节类型 `Bytes`

- 新增不可变的字节序列类型 `Bytes`。`string` 只表示文本（UTF-8）。
- 文件接口分两套：`read_text`/`write_text` 处理 `string`；新增 `read_bytes(path): Bytes`、`write_bytes(path, b)`；`read_chunk` 返回 `Bytes`（现在返回字符串，属不兼容变化，随本项一起改）。
- 转换：`decode(b): string!`（非法 UTF-8 时为错误值）、`encode(s): Bytes`；张量与字节互转 `from_bytes::<f32>(b, shape)`、`to_bytes(t)`，按小端序。类型实参采用设计原则 2.13 已裁定的 `::<T>`，与值上下文参数 `<w>` 分开。
- 原子替换：`fs::replace(path, b)` 写入同目录的临时文件、刷盘后改名。`write_*` 保持原样（技术选择：不改变已有函数的行为）。

### 8.2 检查点

- `save_tree(dir, t)` 与 `load_tree::<T>(dir): T!`：每个叶子一个文件，以 `tree_paths` 命名（张量为 `.npy`，标量与键为文本），另写一个清单（类型结构、dtype、形状、Camel 版本）。先写到临时目录，完成后整体改名发布。载入时按 `T` 检查结构、dtype 与形状，不符时给出路径。目标类型也可按设计原则 2.13 由期望类型推出。
- 训练状态就是一棵树：参数、优化器状态、EMA、键、步数、数据游标。由于随机数由键与步数决定，**随机状态就是两个普通的值**，无需另存生成器的内部状态；恢复后的训练与不中断时逐位相同。
- 数据版本：清单中记录数据集清单的哈希，载入时不符则警告。

### 8.3 分片读取与预取

- 数据集为若干分片文件（`.npy` 或原始字节）加一个清单。读取器的位置是一个普通的值（分片号、偏移），作为游标存进检查点。
- 预取是运行时的实现：读取器声明为外部资源（算子语义第 12 节的资源声明），运行时可提前读取下一批并上传，不改变程序看到的顺序。
- 接口细节待第一阶段之后定（待定 T4）。

### 8.4 长时间运行

DDPM 现在每存一次检查点就重启 Camel。要先查清原因：若是内存或设备缓冲随步数增长，那是缺陷，应当修复而不是用外部脚本掩盖。验收：单进程跑一百万步，主机内存与显存有界（加入一个耗时较长的耐久测试，CI 中以较少步数运行）。

---

## 9. 多卡训练

### 9.1 数据并行的语义

数据并行把一个全局批量切到多张卡上，各自求梯度后求和再平均。梯度对批量的求和被切成多段、再跨卡相加，**求和顺序改变**，结果与单卡不逐位相同。

按设计原则 2.16：

- 跨卡切分这类归约默认允许，不需要写许可。安全模式下 Tiles 不做数据并行，并报告原因。
- 实现使用固定的顺序（各卡内按规范顺序，跨卡按卡号顺序相加），使同样卡数的两次运行逐位相同；不得使用浮点原子加等结果随调度变化的做法。

### 9.2 随机数与切分无关

样本 j 的噪声用 `fold(k, j)` 按**全局样本下标**派生，而不是按卡号。这样无论切成几张卡，每个样本拿到的随机数相同，只有求和顺序不同。

### 9.3 分工

- 核心：集合通信算子的声明（`all_reduce` 等，含资源声明与按卡号顺序的规范语义）；一个卡上出故障时所有卡以同一诊断失败；检查点在多卡下只由一个进程发布。
- Tiles：NCCL 后端、参数复制、批量切分、梯度平均、流与事件、单卡等价性测试（同一全局批量下与单卡结果在规约重排的误差内一致）。
- 对外形式（如 `@gpu(devices: 4)` 或独立的 `data_parallel<step>`）由 Tiles 在多目标设计中定，不是语言语法。

---

## 10. 分工

| 部分 | 核心 | Tiles | 用户代码（如 DDPM） |
|---|---|---|---|
| 参数树 | `tree` 模块、类型规则、`Device<T>` 分配、编译期展开 | `upload`/`download` 接受树 | 删除生成器，改用 `tree_*` |
| 随机数 | `Key` 类型、Philox 规范算法、CPU 实现 | 设备实现，与 CPU 逐位一致 | 改用键式随机数 |
| 一步上设备 | `gather` 等所需算子的声明与定义 | 区域编译、常驻、传输统计与严格模式 | 一个 `train_step` |
| 支持矩阵 | 生成器、由定义推出反向、CI 检查 | 能力声明、由定义推出实现 | |
| 混合精度 | `bf16`/`f16` 元素类型、转换、规范语义、反向 | 设备实现与 Tensor Core 路径（受许可约束） | 显式转换 |
| 显存 | 分析、`@recompute`、梯度累积库函数 | 设备峰值报告 | 标注 |
| 数据与检查点 | `Bytes`、文件接口、`save_tree`/`load_tree`、分片读取器 | 预取上传 | 删除 Python 的分段与发布脚本 |
| 多卡 | 集合通信声明、故障传播 | NCCL、切分、等价性测试、可复现性测试 | 无需许可 |

---

## 11. 阶段与验收

每阶段完成后，在 DDPM 上验收。

1. **参数树与字节类型**：`tree` 模块、`Device<T>` 分配、`upload`/`download` 接受树、`save_tree`/`load_tree`、`Bytes` 与文件接口。
   验收：DDPM 删除 `generate-model-params.py` 与全部 `params_*.cml` 生成文件；检查点含优化器状态并能恢复。
2. **键式随机数与一步上设备**：`Key`、Philox（CPU 与 Tiles）、`gather`、传输统计。
   验收：`train_step` 一次调用完成一步，非报告步的主机与设备之间传输为零字节；恢复训练与不中断逐位相同。
3. **支持矩阵与推出**：生成器、由定义推出反向与 Tiles 实现、CI 检查。
   验收：矩阵入库，DDPM 用到的算子全部"有反向、能上 GPU"。
4. **混合精度**：`bf16`/`f16` 张量、转换、规范语义与反向。
   验收：DDPM 以 bf16 前向训练，主权重 f32；对照 f32 训练的损失曲线。
5. **显存**：分析、`@recompute`、梯度累积。
   验收：报告的峰值与 Tiles 实测一致；重计算前后损失逐位相同。
6. **长时间运行与数据**：耐久测试、分片读取与预取。
   验收：DDPM 单进程训练到底，无需外部脚本分段。
7. **多卡**：集合通信、数据并行。
   验收：四卡与单卡在同一全局批量下损失曲线一致（规约重排误差内）；四卡两次运行逐位相同；吞吐对比单卡。

---

## 12. 待定问题

实现到相应阶段前再问语言作者；括号内为建议。

- **T1** 数组是否作为树的内部节点（暂不；需要时用元组或单独设计）。
- **T2** 同一函数中重复使用同一个键时是否给出提示（以后作为可选检查）。
- **T3** 重计算标注的名称与位置（`@recompute`，写在函数或块上，由 autodiff 模块导出）。
- **T4** 分片读取器的接口（第一、二阶段完成后按 DDPM 的实际需要定）。
- **T5** `read_chunk` 改为返回 `Bytes` 的不兼容变化是否接受（接受，随第一阶段一起改，并更新所有调用处）。
