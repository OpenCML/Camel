# 泛型按结构展开：设计稿（待作者讨论）

状态：提案，未实现。服务于 [设计原则 2.17](../../design-principles.cn.md) 第一条：`tree_map` 这类"形状由类型决定、编译期展开"的操作用泛型写在库里，不做编译器内建；为此泛型要能在编译期按结构体、元组的字段递归。

本稿里的 Camel 代码都是**示意**：语法尚未定，只用来比较方案。设计原则第 6 节"尚未决定的问题"（类型表达式语言、`typeof`、类型函数不终止时的处理）只引用，不替作者决定。

---

## 1. 问题

现状（审查 `frontend.cn.md` 1、2、4，`modules.cn.md` 1、8）：

- `src/compile/gir/tree.cpp`（236 行）在建图时按字符串 `tree:tree_map` 等认出五个操作并自己展开。`tree` 模块是空壳，它们的 resolver 恒为空，因此这些名字不能当函数值用，`inspect --ops` 与悬停也拿不到签名。
- "什么是内部节点、叶子顺序、路径怎么写"在 `tree.cpp`、`nn/checkpoint.cpp`、`tiles/device.cpp`、`tree_transfer.cpp`、`module.cpp` 各写一份，已经不一致：有的把 `T?` 内部的元组当树递归，有的拒绝空结构体，有的以 `$` 为根。
- `nn.save_tree` 的内核在运行时读 `norm.type(1)` 遍历值的静态类型，违反"原生内核不在运行时读取调用的静态类型"（2.17）。
- 之后每加一个按类型展开的操作（序列化、逐字段相等与打印、`Device<T>` 分配、`tree_flatten`……）都要改编译器。

2.9 已经定了方向：泛型函数按实参类型实例化，图上只有具体类型。缺的只是：**实例化时函数体能不能按类型的结构生成不同的图**。现在不能：函数体对每个类型都是同一段代码。

## 2. 需求

一个能写成库函数的 `tree_map`，至少需要下面八件事。后面每个方案都按它们对照。

| # | 需求 | 例子 |
|---|---|---|
| R1 | 按声明顺序遍历结构体、元组的字段，得到字段名（元组为下标）与字段类型 | `{ a: A, b: (B, C) }` 依次是 `a`、`b` |
| R2 | 对子项递归，递归按子项的类型再实例化一次 | `b` 里再展开 `0`、`1` |
| R3 | 在叶子处停止。叶子是内部节点以外的一切：标量、张量、字符串、数组、映射、`enum`、`Device<标量或张量>`，以及 `T?`、`T!`（带标记的元组与单元格不是内部节点，2.16、审查 frontend-4） | `Tensor<f32>?` 是叶子，不是一元素的元组 |
| R4 | 构造同形的结构体或元组，每个字段换成新类型，字段名与顺序不变 | `{ w: Tensor<f32> }` 变成 `{ w: float }` |
| R5 | 沿途带着路径 | `blocks.0.w`，根为空路径（2.17） |
| R6 | 同时遍历多棵同形的树，对位的叶子作为多个实参传给回调；边界以第一棵为准 | `tree_map<f>(p, g, m, v)` |
| R7 | 空树（`{}`、`()`）正常工作，不特殊处理 | `tree_reduce` 的结果就是初值 |
| R8 | 全部在实例化时解决：展开后的图只有逐叶的普通节点（ACCS、FILL、对回调的调用），没有"类型""字段表"这类值；回调是泛型函数或重载集合时，每个叶子按自己的类型各实例化一次 | 2.16、训练设计稿 3.1 |

另有几条来自已有裁定的约束：

- `with` 参数不是泛型参数；`macro` 不是类型计算的基础（2.9）。所以不能靠 `macro` 展开，回调 `f` 仍写在 `<>` 里作为上下文参数。
- 叶子边界由树实参的类型决定，不由回调的形参类型决定（2.16）。
- 实例化必须终止；失败要有指向叶子的诊断。
- 不另做过渡机制：新增的语言构造要小，并且是将来类型函数、序列化等同样会用到的那一套。

## 3. 候选方案

三个方案共用的背景：编译器已经有"泛型函数 = 模板，调用时按实参类型实例化并缓存"（`builder.cpp` 的 `GenericTemplate` / `instantiateGeneric`）。`tree.cpp` 现在做的事，换成实例化的说法就是：每个结构体类型的 `tree_map` 实例，函数体是"对每个字段取 ACCS、递归调用自己、最后 FILL"。三个方案的区别只在于**用户怎样在 Camel 里写出这句话**。

### 3.1 方案 A：编译期字段展开（`match type` + `static for`）

借鉴：Zig 的 `inline for` 与 `@typeInfo`、C++26 反射的 `template for`、Julia 的 `@generated` 函数。

新增的构造，共四样：

1. `match type T { case struct => …; case tuple => …; case _ => … }`：按类型的**形状**在实例化时选一条分支，**没被选中的分支不建图、不做类型检查**（相当于 C++ 的 `if constexpr`）。`struct`、`tuple` 只匹配 R3 定义的内部节点，所以 `T?`、`T!`、单元格落在 `case _`。
2. `static for (constas n, typeas F) in fields T`：把字段表展开，每个字段一份函数体。`n` 是编译期常量（结构体是字段名，元组是下标的十进制字面，两者都能直接当路径的一段），`F` 是字段类型。绑定写法沿用 `constas`、`typeas`（2.9、2.13），不引入新的绑定机制。
3. 编译期字段访问 `x.[n]`，以及"推导式"构造 `{ static for … => n: e }`（结构体）与 `( static for … => e )`（元组）：生成同形的新值，字段类型由 `e` 的类型得到。
4. 变长参数 `..ts` 与展开调用 `f(t, ..ts)`：`ts` 是元数在编译期已知的元组，`static for (constas j) in indices ts` 遍历它。解决 R6。

另外，带 `static` 构造的泛型函数**可以省略返回类型**，由实例化后的函数体推出（见 3.1.4）。

#### 3.1.1 `tree_map`

```camel
// 回调 f 作为 with 参数；t 决定叶子边界；ts 是其余同形的树（元数可以是 0）
export func tree_map<f>(t: typeas T, ..ts) {
    return match type T {
        case struct => { static for (constas n, typeas F) in fields T =>
                             n: tree_map<f>(t.[n], ..column(n, ts)) }
        case tuple  => ( static for (constas n, typeas F) in fields T =>
                             tree_map<f>(t.[n], ..column(n, ts)) )
        case _      => f(t, ..ts)
    }
}

// 取各棵树在字段 n 处的子树。找不到字段就是普通的"无此字段"错误
func column(constas n, ts) {
    return ( static for (constas j) in indices ts => ts.[j].[n] )
}
```

要点：

- 对 `{ w: Tensor<f32>, b: Tensor<f32> }`，实例展开成 `{ w: f(t.w, ts.0.w), b: f(t.b, ts.0.b) }`；`f` 是泛型函数或重载集合时，`f(t, ..ts)` 就是一次普通调用，按此处实参类型选择、实例化，和现在 `tree.cpp` 里的 `invoke` 一致。
- 同一个类型只实例化一次：12 个相同的 `Block` 组成的元组，`Block` 的实例只有一份，元组那一层展开成 12 次对它的调用。
- 其余树与第一棵形状不同时：缺字段由 `ts.[j].[n]` 报"类型 X 没有字段 n"；**多出**的字段不会被发现。库里另有一个同样用 `match type` 写的 `same_shape(a, b)`，`tree_map` 开头调用它（略，约 12 行）。

#### 3.1.2 `tree_paths`

```camel
export func tree_paths(t: typeas T) { return paths_at(t, "") }

func paths_at(t: typeas T, here: string) {
    return match type T {
        case struct => { static for (constas n, typeas F) in fields T =>
                             n: paths_at(t.[n], join(here, n)) }
        case tuple  => ( static for (constas n, typeas F) in fields T =>
                             paths_at(t.[n], join(here, n)) )
        case _      => here
    }
}
func join(a: string, b: string): string { return if a == "" then b else a + "." + b }
```

`tree_paths` 的结果是与 `t` 同形、叶子换成路径字符串的树。路径是普通的字符串值：`here` 是实参，`join` 的结果对每个叶子是常量，由常量折叠遍化为字符串常量节点（和现在的 `addStaticDataNode` 一样）。**实例按类型共享**，不因路径不同而多份；折叠不是语义所依赖的。路径是值，不是类型。

#### 3.1.3 `save_tree` / `load_tree`

现在的内核在运行时遍历值与类型。改写后内核只认叶子，顺序由数据依赖保证，类型信息由编译期展开写进"结构说明"字符串：

```camel
// 原生算子（nn 模块，C++）：内核不读静态类型，每种叶子一个重载
//   ckpt_begin(dir: string): Ckpt
//   ckpt_put(h: Ckpt, path: string, x: Tensor<f32>): Ckpt     // 重载：float、Tensor<i64>、Key……
//   ckpt_commit(h: Ckpt, spec: string, data_hash: string): unit!
//   ckpt_open(dir: string, spec: string): Ckpt!               // 一次校验清单、结构、全部文件
//   ckpt_get::<L>(h: Ckpt, path: string): L                   // 叶子重载，L 为显式类型实参

export func save_tree(dir: string, t: typeas T): unit! {
    let h = save_at(ckpt_begin(dir), "", t)       // 沿规范顺序一路传下去，写入次序就是数据依赖
    return ckpt_commit(h, type_spec::<T>(), "")
}
func save_at(h: Ckpt, here: string, t: typeas T): Ckpt {
    return match type T {
        case struct | tuple =>
            static loop h = h for (constas n, typeas F) in fields T do   // 展开的折叠
                save_at(h, join(here, n), t.[n])
        case _ => ckpt_put(h, here, t)             // 没有适用的重载：带路径的编译错误
    }
}

export func load_tree::<T>(dir: string): T! {
    return ckpt_open(dir, type_spec::<T>()) ?-> (h => load_at::<T>(h, ""))
}
func load_at::<T>(h: Ckpt, here: string): T {
    return match type T {
        case struct => { static for (constas n, typeas F) in fields T => n: load_at::<F>(h, join(here, n)) }
        case tuple  => ( static for (constas n, typeas F) in fields T => load_at::<F>(h, join(here, n)) )
        case _      => ckpt_get::<T>(h, here)
    }
}
// 结构说明：同样的 match type，返回字符串，如 "{blocks:(…),head:{w:f32[3,4]}}"
func type_spec::<T>(): string { … }
```

说明：

- 写入顺序 = 展开顺序 = 规范顺序（2.16、2.17），`save_tree` 的路径与 `tree_paths` 的结果逐字一致，因为用的是同一个 `join`；`nn/checkpoint.cpp` 里复制的 `internal/sizeOf/fieldType/fieldName/pathAt` 全部可以删。
- `load_tree` 的所有可能失败（文件缺失、清单与 `T` 的结构不符、校验和）集中在 `ckpt_open` 一处，返回 `Ckpt!`；之后逐叶的 `ckpt_get` 不再返回错误。这是对现在实现的一个改动（现在在内核内一次完成），需要 nn 一侧确认（问题 11）。
- `type_spec::<T>` 是 `match type` 的又一个用法，说明字符串也能由类型算出，不需要运行时读类型。
- `static loop … for … do` 就是 2.13 的循环头写法加上 `static`，状态在展开中串起来；不另造折叠语法。

#### 3.1.4 编译器怎么处理

- 解析：`match type`、`static for`、`static loop`、`x.[n]`、推导式、`..`。
- 实例化（`builder.cpp`）：函数体建图时，遇到 `match type` 就用已绑定的类型选分支，只建所选分支；遇到 `static for` 把字段表（来自 `StructType`/`TupleType`，即现在 `tree.cpp` 里 `sizeOf/fieldType/fieldName` 做的事）展开成 N 份循环体，每份把 `n`、`F` 绑定为常量与类型；`x.[n]` 直接产生 ACCS 节点；推导式产生 FILL 节点（`tree.cpp` 的 `pack` 即是）。这些节点现在都由 `tree.cpp` 手工建，改后由通用的建图路径建。
- 实例的缓存键：（模板，各实参类型，**`with` 参数里的函数值的身份**）。`tree_map<f>` 对不同的 `f` 是不同的实例，和 C++ 里函子作模板参数一样。`f` 若是泛型函数，作为名字传入，到调用点才按叶子类型实例化。
- 返回类型推断：带 `static` 构造而省略返回类型的泛型函数，实例的返回类型取函数体的类型。子项的实例总是比外层的实例先完成（类型严格变小），所以不需要不动点；若递归回到一个正在实例化的相同键，报错。
- 终止：结构体与元组不能直接包含自身，递归类型必须经过数组、`enum` 或 `Map`，这些在 R3 下都是叶子。因此展开按类型的树高终止；仍然给实例化深度一个可调的上限（同 2.14 对宏循环的做法），防止用户写出自我调用的 `match type`。
- 诊断：每个实例帧记录它的静态绑定，出错时给出实例链：

  ```
  错误：没有重载接受 ckpt_put(Ckpt, string, Tensor<i64>)
    在 save_at::<Tensor<i64>>
    在 save_at::<{ w: Tensor<i64> }>     n = "w"
    在 save_at::<(Block, Block)>         n = "1"
    在 save_tree::<{ blocks: (Block, Block) }>  at train.cml:41
  ```

  链上的 `n` 连起来就是路径 `blocks.1.w`。`case _ => static error "…"` 允许库给出更直白的消息。

### 3.2 方案 B：头/余递归的类型匹配（Haskell 泛型风格）

借鉴：GHC.Generics、Rust 里为元组逐元数 `impl`、`HList`。

不引入循环，只引入"类型上的模式匹配 + 剩余部分"：`match type` 的模式可以写 `(typeas H, ..typeas R)` 与 `{ constas n: typeas H, ..typeas R }`，值上对应 `let (h, ..r) = t` 与 `{ n: x, ..rest }`。

```camel
export func tree_map<f>(t: typeas T, ..ts) {
    return match type T {
        case ()                    => ()
        case (typeas H, ..typeas R) => {
            let (h, ..r) = t
            return (tree_map<f>(h, ..heads(ts)), ..tree_map<f>(r, ..tails(ts)))
        }
        case {}                    => {}
        case { constas n: typeas H, ..typeas R } => {
            let { n: h, ..r } = t
            return { n: tree_map<f>(h, ..heads(ts)), ..tree_map<f>(r, ..tails(ts)) }
        }
        case _                     => f(t, ..ts)
    }
}
```

`tree_paths` 同理，但要给每层传前缀，并对"后面的字段"重新传同一个前缀。

优点：模式匹配是 `enum` 的 `match`（2.13）的自然推广；结果类型可以由类型函数写出，不必推断；终止显然（每步字段变少）。
缺点（对本问题是实质性的）：

- 每个后缀都是一个新类型：100 个字段的结构体要 100 个互不相同的实例，递归深度 100，且每一层都要重新构造"剩下的字段"这个值（`heads/tails` 对 `ts` 同样）。图上会出现 O(n²) 的中间结构体，优化后虽会消失，但建图期的代价和递归深度上限都成问题。Transformer 的块、每块十几个张量正是这种规模。
- 需要类型层的"余部"与展开构造（`..R`），那是类型表达式语言的核心，等于把第 6 节的未决问题先定下来。
- 读起来像函数式列表递归，对写数值程序的人不自然。

### 3.3 方案 C：trait 与派生实例

借鉴：Rust 的 `trait` + `#[derive]`（serde 就是这样做的）、JAX 的 `register_pytree_node`。

```camel
trait Tree {
    func tree_map<f>(self, ..others: Self): Self'    // 结果类型随 f 变化，需要关联类型
}
impl Tree for Tensor<f32> { … }
derive Tree for struct, tuple                       // 谁来实现 derive？
```

问题：

- 新增整套 `trait`/`impl`/一致性/孤儿规则/关联类型，是 Camel 目前没有也没有计划的东西（2.7 提到 Camel 不支持面向对象，配置靠 `with` 参数）。
- `derive` 对结构体生成"逐字段"代码，这件事要么是编译器内建（又回到 `tree.cpp`），要么要用户能写"对字段循环"的代码，即方案 A 的 `static for`。所以方案 C 底下仍需要方案 A 的机制，只是多包了一层。
- 优点是真实的：可扩展。用户可以声明"这个配置结构整体是叶子"（训练设计稿 3.2 提到的 `Leaf<T>` 需求），也可以让数组成为内部节点（T1）。方案 A 在这点上要靠将来另加一个"叶子"标注，见问题 5。

### 3.4 三个方案的比较

| | A 字段展开 | B 头/余递归 | C trait + derive |
|---|---|---|---|
| R1–R8 | 全部满足 | 满足，R5、R6 笨拙 | 取决于 derive 的实现 |
| 新增构造 | `match type`、`static for/loop`、`.[n]`、推导式、`..` | `match type`（含余部模式）、`..`、类型层展开 | `trait`、`impl`、`derive`、关联类型 |
| n 个字段的结构体的实例数 | 1 | n | 1 |
| 与现有的衔接 | 展开的就是 `tree.cpp` 现在手写的三个步骤 | 与 `enum` 的 `match` 同构 | 全新 |
| 用户自定义叶子 | 将来加标注 | 将来加标注 | 天然支持 |
| 结果类型 | 由函数体推断 | 可由类型函数写出 | 关联类型 |

### 3.5 各方案共同的处理

**叶子的具体规则。** 都用 R3：`struct`、`tuple` 两个模式只匹配未带标记、非单元格的结构体与元组。这就是审查 frontend-4 要的"唯一的树视图"：C++ 里留一个 `core::type::tree::isInternal(Type*)` 与字段遍历，`match type` 与 Tiles、nn 的 C++ 代码共用；路径的拼法只有库里的 `join` 一处（C++ 不再自己拼）。

**`T?`、`T!`。** 叶子，整体交给回调或 `ckpt_put`。想在 `Tensor<f32>?` 里递归的话，库自己写；本稿不为它加模式（语言还没有 `case T?` 的类型模式）。

**`Device<T>`。** 类型构造时已经把 `Device<{ a: A }>` 规范化成 `{ a: Device<A> }`（2.16），所以 `match type` 看到的是结构体，字段是 `Device<A>` 叶子，`tree_map` 不需要知道它。`Device<T>` 的规范化本身目前在 C++（`tiles/device.cpp`），它是"按字段分配的类型映射"，用方案 A 的两个构造在**类型位置**写就是：

```camel
type Device<T> = match type T {
    case struct => { static for (constas n, typeas F) in fields T => n: Device<F> }
    case tuple  => ( static for (constas n, typeas F) in fields T => Device<F> )
    case _      => DeviceLeaf<T>            // 叶子必须是 Device 支持的类型，否则 static error
}
```

这正是第 6 节"类型函数里的条件与匹配怎么写"。本稿的建议：类型位置与值位置用同一套 `match type`、推导式，且**只加这两样**；`typeof`、`keyof`、`&`、`\` 等仍按第 6 节待定，不受影响。`Device` 的类型层版本不属于第一步，见第 5 节。

**数组、`enum`、`Map`。** 都是叶子（训练设计稿 3.1，T1）。需要时加一个类型模式 `case array` 并配合运行时长度，不在本稿。`enum` 的变体不能由 `static for` 展开（它们在运行时才分辨，2.13 已排除运行时类型判断）；将来要"对每个变体"展开，应当是 `fields` 作用于 `enum` 得到变体表，再生成 `match` 的各臂，仍在同一框架内。

**空树。** `fields {}` 为空，推导式得 `{}` 或 `()`，`static loop` 直接返回初值，不需要特殊分支。`tree_unzip` 在空参照树上仍无法推出元组元数（现在也是这样报错），可以用显式类型实参解决，不属于本稿。

**`tree_reduce` 与 `tree_unzip`。** `tree_reduce` 是 `static loop`（同 `save_at`）。`tree_unzip(r, like)` 最难：要先由 `like` 的第一个叶子位置对应的 `r` 的元组元数 k 求出 k，再生成 k 棵树。用方案 A 约 25 行，需要一个按同样方式写的 `first_arity` 辅助函数；不是新机制。

## 4. 建议

**建议方案 A，第一步只做值位置。** 理由：

1. 它把现在 `tree.cpp` 手写的三件事（取字段、递归、打包）原样变成语言能表达的构造，编译器侧是"通用化"，不是"另写一套"；方案 B 要先定类型层的余部，方案 C 底下仍要 A。
2. 实例按类型共享，不随字段数增长，建图期递归深度等于树的深度，不等于字段数。
3. 满足 2.16、2.17 对"图上只有逐叶的普通节点"的要求：展开发生在实例化，原生内核只认叶子，`save_tree` 不再在运行时读类型。
4. 构造数量最少，而且都是别处也要用的：`match type`、推导式是类型函数（2.9、第 6 节）要的东西；`..` 是任何可变元数泛型要的东西。

**编译器代价**（粗估）：

- 删除：`tree.cpp` 236 行、`builder.cpp` 里按 URI 的入口；`nn/checkpoint.cpp` 里遍历类型的 `snapshot` 改为逐叶重载（约 -250 行，新内核约 +80 行）；`tiles` 的 `supportedLeaves`、`transferTreeType` 中的 `walk` 之后可以改为库里的泛型函数。
- 新增：解析与 GCT 节点（五个新构造）；建图器里 `match type` 选分支、`static for/loop` 展开、静态字段访问、推导式（复用现有的 ACCS/FILL 建法）；返回类型推断；实例缓存键加入 `with` 里的函数身份；实例链诊断。预计建图器里约 +600 行，是这条线的主要工作量。
- 风险：(a) 展开后的实例是一组 CALL 节点，不是平的节点；2.16 要求自动微分与 Tiles 看到的是普通的图，因此含静态构造的实例必须在它们之前内联（问题 7）。(b) 实例缓存键加入函数身份后，用 lambda 作回调的每个调用点各自一个实例，与现在 `tree.cpp` 每个调用点各展开一次相同。(c) 返回类型推断是新能力，限定在带静态构造的泛型函数里。

**还能带来什么**（都是同一套构造，不再改编译器）：

- 逐字段的 `==`、`print`、哈希：`match type` + `static loop`；配合运算符重载（2.13，用户的更具体者优先），可为任意结构体提供通用的 `operator ==`，而不是内建。
- JSON 与序列化：`json::encode(x)` 与 `decode_as::<T>`（2.13）由 `fields T` 生成。
- `tree_zip`、`tree_flatten`、`tree_count`、`tree_filter`（冻结部分参数）、梯度累积、EMA 等训练设施，全部是库代码。
- `Device<T>` 的类型层版本（见 3.5），以及 Tiles 的叶子检查改成库里的泛型函数，叶子集合只在一处定义。
- 将来的"用户自定义叶子"（一个标注）与"数组作内部节点"（T1）：在 `match type` 里多一个 `case`，不动核心。

## 5. 分期

1. 值位置：`match type`、`static for/loop`、`x.[n]`、推导式、`..`、返回类型推断、实例链诊断；把 `tree_*` 改为库函数，删 `tree.cpp`；`save_tree/load_tree` 改为逐叶内核。
2. 类型位置：`match type` 与推导式用于类型函数，改写 `Device<T>`（与作者对第 6 节的决定同步）。
3. 其余：`json` 的 `decode_as::<T>`、逐字段 `==`/`print`、叶子标注。

## 6. 向作者提的问题

**1. 选哪个方案？**
建议 A（编译期字段展开）。若倾向 B，需要先定类型层的余部写法（`..R`）和对 100 字段结构体 100 个实例的接受程度；若倾向 C，需要同时承诺 `trait` 这个新的语言特性。

**2. 带静态构造的泛型函数可以省略返回类型，由实例化后的函数体推出吗？**
例：`func tree_map<f>(t: typeas T, ..ts) { return match type T { … } }`，不写 `: …`。返回类型依赖回调的返回类型，用户写不出（没有类型层的"调用结果"）。建议：只允许泛型函数省略，且只有函数体含 `match type`/`static` 时；递归回到正在实例化的同一个键时报错。

**3. 同时遍历多棵树，用变长参数 `..ts` 与展开调用 `f(t, ..ts)`，还是为 1 到 N 棵树分别写重载？**
例：`tree_map<adam_leaf>(p, g, m, v)`。建议：变长，`..` 绑定一个元数在编译期已知的元组，元数 0 合法；不引入运行时变长。

**4. 静态构造的拼写。**
候选：`match type T { case struct => … }`；`static for (constas n, typeas F) in fields T => …`；`static loop acc = init for (…) in fields T do …`；`t.[n]`；`{ … => n: e }`。
问：(a) `match type` 与 2.13 的值的 `match` 共用 `match` 再加 `type`，是否可接受；(b) 字段访问写 `t.[n]`、`t.(n)` 还是 `field(t, n)`；(c) 构造结构体时 `n: e` 的 `n` 是编译期名字，而平时 `n: e` 的 `n` 是字面的字段名，是否需要标记（如 `[n]: e`）。建议：沿用上面的写法，(c) 加方括号 `[n]: e` 更不易混淆。

**5. 叶子怎么定义，用户能不能把自己的结构体声明成叶子？**
例：`type Config = { lr: float, steps: int }` 放进参数树，想让 `tree_map` 把它当整体。现在 `case struct` 一律展开它。建议：第一阶段不加，规则固定为 R3（与 2.16、审查 frontend-4 一致）；第二阶段加一个类型标注（如 `@leaf type Config = …`），让 `case struct` 跳过它；不为此引入 `trait`。

**6. 以函数值为 `with` 参数的泛型，函数身份是否成为实例键的一部分？**
例：`tree_map<(a: Tensor<f32>): float => sum(a)>(t)` 与 `tree_map<other>(t)` 是两个实例。这与 2.9"`with` 参数不是泛型参数"不矛盾（`f` 不是类型参数，只是它作为值决定了实例），但需要作者确认这个读法。建议：确认；回调为泛型函数时按名字传入，在叶子处才按叶子类型实例化（现在的行为）。

**7. 展开出来的实例要不要强制内联？**
例：`tree_map<f>(p, g)` 展开为对 `tree_map<f>::<Block>` 的 12 次调用。2.16 说自动微分与 Tiles 看到的是普通节点。建议：函数体含 `match type`/`static for` 的实例一律内联进调用者（用户不必写标注），内联之后与现在 `tree.cpp` 展开的图一致；2.17 的"优化不改变算子身份"不受影响，因为内联的是用户定义的函数图，不是内建算子。

**8. 编译期展开失败的诊断，怎么给路径？**
例：`save_tree(dir, { blocks: (b0, b1) })` 里 `b1.w` 是 `Tensor<i64>`，没有对应重载。建议：实例链加静态绑定（3.1.4 的格式），链上 `n` 连起来是 `blocks.1.w`；另提供 `static error "…"`，允许库给出自己的消息；实例化深度上限可调（同 2.14 对宏循环的做法）。

**9. 路径是运行期字符串（折叠为常量）还是编译期常量参数？**
例：`paths_at(t, here)` 的 `here` 是普通 `string` 实参，对每个叶子折叠成常量；若改成 `constas` 字符串，实例随路径各一份（12 个 Block 就是 12 份），且不能再被多个调用点共用。建议：运行期字符串，靠折叠；路径与诊断里的 `n` 分开，各管各的。

**10. 类型位置要不要同时加 `match type` 与推导式（用来写 `Device<T>` 和 `tree_unzip` 的结果类型）？**
例：3.5 的 `type Device<T> = match type T { … }`。第 6 节还没定类型函数里的条件与匹配。建议：值位置先做（第一期），类型位置作为第二期，两者用同一套写法，并且只加这两样，不借此决定 `typeof`、`keyof`。

**11. `load_tree` 改成 `ckpt_open` 一次校验、之后逐叶读取不再出错，可以吗？**
例：`load_tree::<T>(dir)` 现在在内核里一次读完并返回 `T!`；改后 `ckpt_open` 返回 `Ckpt!`，结构说明与清单不符、文件缺失、校验和错都在这里报，之后 `ckpt_get` 只会遇到故障（文件在校验之后被改动）。建议：可以；这是消掉"每个叶子都可能是 `F!`，如何组合成 `T!`"这个问题的最简单做法。
