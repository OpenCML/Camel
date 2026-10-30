# 问题：去函数化的验收数不到 `grad` 造出的那个函数值

## 现象

**这个数已经是 0 了，而它指的改动一行都没做。** 一个带函数调用、分支和循环的 `with_grad` 小程序：

```camel
import { grad } from autodiff

with <w: float>
func h(x: float): float { return w * x + 1.0 }

with <w: float>
func f(n: int): float {
    let base = if w > 0.0 then h<w>(1.0) else h<w>(2.0)
    let total = loop acc = base for i in 0..n do acc + h<w>(float(i))
    return total
}

func main(): int sync { println(grad(f)<2.0>(4)); return 0 }
```

主机上这一代构建（stamp 与树一致）读 `std::stats`（`src/passes/trans/stats/stats.cpp#collectGraphStats`）：

| 流水线 | closures | indirect_calls | nodes | edges | specializations |
|---|---|---|---|---|---|
| 原样 | 0 | 2 | 35 | 36 | 0 |
| `std::macro` | **4** | 7 | 165 | 184 | 0 |
| `std::opt` | **0** | **2** | 47 | 46 | **2** |
| `std::macro std::opt` | **8** | 8 | 319 | 413 | 0 |

两点：

- **`std::opt` 后的 0 是两个副本换来的**（`specializations 2`），不是标签的表示。设计稿说按调用点复制不是等价替代；现在的 0 正是复制。三个读数放在一起才说明这一点：`std::macro` 单跑 **4 / 7**，`std::macro std::opt` 读 **8 / 8**，直落 `std::opt` 才是 **0 / 2**——同一份程序，那个 0 只在带上那两个特化副本时出现。
- **剩下的 2 次间接调用，被调者是算子造出来的值**：`std::gir` 里那个 `CALL` 的 with 输入 0 是一条 `OPER autodiff:grad` 节点，不是 `FUNC`、也不是静态函数值。`modules/autodiff/operators.cpp#gradFunction` 在**运行期**调 `buildGradientGraph` 造出整张求导图，再 `::Function::create` 出一个函数值返回；调用点拿到的是一个运行期才知道的函数。

于是"闭包表示成标签加环境"这一条遇到一个没有变体的函数值：标签法的变体集合是"程序里被取作值的函数"，而 `grad(f)` 这段东西不是程序写下来的函数，是算子算出来的。同一处还让数看不见东西：`gradFunction` 把 f 的闭包拷进运行期 `::Function` 的 `tuple()`，那个闭包不在任何图里，`std::stats` 与 `std::gir` 都数不到（`GCFillKind::FunctionClosure` 只数图上的 `FILL`）。

顺带记下的读数，供"这一条到底动多少"参考：这段表示的三个拼写（`createFuncDataNode`、`GCFillKind::FunctionClosure`、`GCNodeKind::Call`）在 `src/`、`include/`、`modules/`、`stdlib/`、`tools/` 里共 **144 处 / 59 个文件**，真正创建或按它分派的代码点 **38 个**（`createFuncDataNode` 的调用 25、`pullback.cpp` 里 `addClosure` 的调用 3、`addCallNode` 的创建调用 10；闭包载荷本身只在两处拼出来，`builder_calls.cpp` 的 lambda 与 `addClosure`）。

## 为什么不能自己定

- **压到的是 9.9 与 6.2 之外的一条**：9.9 说自动微分是"建立在遍机制之上的库"，图里没有回传闭包；`grad` 现在是一个运行期算子，它自己造图、自己持闭包。要让去函数化把 `grad(f)<w>(x)` 变直接调用，就得先让这张图在建图期就存在——那是第 6 条（`reinterp`/`split`/`transpose` 写进库）在做的事，两条线会撞在同一处。
- **不搬也可以，但那是改裁定**：给"算子造出的函数值"留一个运行期标签的变体，等于把"闭包只在离开程序时留下"放宽成"算子造出的也算"，并且 0/0 这个验收在原程序上永远不成立。
- **验收口径也不归施工方**：同一份程序，`std::opt` 读 0/2，`std::macro std::opt` 读 8/8，两个都是"整轮优化后"。照哪个数做，决定的是做不做得到，不是做得好不好。

## 选项

**A（默认建议）本条只管"程序里写出来的函数值"，`grad` 的运行期造图归第 6 条。**

- 要额外造什么：本条按设计稿把函数值换成 `enum`（标签 + 环境），把间接调用换成按标签的 `match`、标签已知时折掉；**不碰** `gradFunction`，也就没有新机制。
- 验收数会变成多少：上面那个小程序经 `std::opt` 后 **`indirect_calls` 2 → 0**、`closures` 0（不变）、`specializations` 2（只记录，不作门槛）。第 6 条落地之前，`grad` 造出的那条调用不在这个口径里，页里要写清这条限制。
- 三个选项里，只有 A 与 B 让验收数从"已经是 0"变成"**2 → 0**"；A 是其中不用先动求导建图时机的那一个。

**B 归本条，先做前置。**

- 要额外造什么：把 `modules/autodiff/operators.cpp#gradFunction` 的 `buildGradientGraph` 从运行期搬到建图期（`GradResolver` 已经是建图期的入口，出的是签名），让 `grad(f)<w>(x)` 的被调者在编译期已知；然后照 A 做表示改造。
- 验收数会变成多少：**`indirect_calls` 2 → 0**，且不依赖第 6 条；`closures` 0；`specializations` 只记录。代价是这一笔动求导的建图时机，与第 6 条第 3 步重叠。

**C 验收只数图上的 `FILL(FunctionClosure)`，间接调用不计数。**

- 要额外造什么：什么也不用造——这个数改动前后都是 0。
- 验收数会变成多少：`closures` **0 → 0**，`indirect_calls` 保持 2 且不入验收。它拦不住任何回归，选它等于这一条没有可测的验收。

## 要作者答的一句

第 7 条按 A 还是 B？（两者都把验收数定成间接调用 **2 → 0**：A 把 `grad` 的运行期造图让给第 6 条，B 在本条先把求导图搬到建图期。）