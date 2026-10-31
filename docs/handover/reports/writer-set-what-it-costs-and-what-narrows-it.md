# 报告：保守写者集合的代价，与收窄它的下一步

> 线：`lane-writers`（分支 `lane-writers`，工作树 `h-writers`）。
> 相关：`docs/handover/reviews/questions-2026-10-31-two-references-write-one-object-outside-a-sync-block.md`（裁定与判据）、`docs/design-principles.cn.md` 3.1 / 3.5、`docs/design/effects/edges.cn.md`。
> 读数全部来自构建后实跑（远程 `-Plan`），不是代码推演。

## 1. 三笔，各自的代价（对数）

取样：`test/cases/resources/effects.cml`（三个变量：`c` 被闭包捕获写、`xs` 被自己写、`t` 是两个函数的形参）。"对"= 集合里的一个 (变量, 写者函数) 条目。

| 笔 | 提交 | 做了什么 | names | pairs |
| --- | --- | --- | --- | --- |
| 写者集合 | `4cdcff6f5` | 整程序扫描，穿过函数值的调用算作可见变量的可能写者 | 3 | 5 |
| 收窄·形参 | `8e117097e` | 写自己形参的函数不算写者 | 2 | 3 |
| 收窄·局部 | `d368b274b` | 写自己声明的 `var` 的函数不算写者 | 1 | 1 |

- 形参那一笔去掉的是 `t`：`bump` 和 `countdown` 各自的形参，两对完全无关。
- 局部那一笔去掉的是 `xs`（`main` 声明并写，没有别处写）与 `main` 对 `c` 的那一对；`c` 仍留在集合里，因为闭包 `inc` 也写它（捕获的那一对靠闭包自己撑着，不靠声明函数）。
- 同一取样里"每个变量的写者函数"是 1–2 个，**没有跨模块的写者**。
- 验收程序 `test/cases/effects/two_references_write_one_object_unsynced.cml`：names 1、pairs 2（`counter` 由 `twice` 和传入的 lambda 各写一次），改前打 `1`、改后打 `3`。

## 2. 三条已知边界，以及"今天给出什么数"

每一条都配一个最小程序和**实跑读数**（`std::nvm`，四个后端同数）。"对的数"指按语言语义应得的输出。

1. **只经 `var` 实参传出去才被写的变量不在扫描里。** 调用点没有被调者的类型，说不出"这次实参是哪个单元的引用"。
   ```camel
   var counter = 0
   func apply(f: (var x: int) => void) { f(counter) }
   func main(): int sync { apply((var y: int) => { y = 3 }); println(counter); return 0 }
   ```
   **读数是 `3`，对的数也是 `3`。** 今天对不是集合算出来的：调用点把 `counter` 的单元交给 `f`，写入落在同一个单元上。会错的形状是读要的是一份"调用之前的拷贝"而不是那个单元（例如读发生在另一个 `var` 传递链的另一端），**该形状未测**——构造它需要把单元再交出去一层，超出这个最小程序。

2. **调用之后才第一次捕获的变量，按书写顺序当"之后"**（`claimedWriters_`）。
   ```camel
   var counter = 1
   func run(f: () => void): int { f(); let read = () => counter; return read() }
   func main(): int sync { println(run(() => { counter = 5 })); return 0 }
   ```
   **读数是 `1`，对的数是 `5`——今天给出错的数。** 调用在 `run` 里、读写在调用之后才写下的那个闭包里，读拿的是 `run` 捕获时的版本。

3. **两次相邻的未知调用，前一次本身也是未知调用修饰时，不串 `writeBeforeCall_`。**
   ```camel
   var counter = 0
   func run(a: () => void, b: () => void): int { counter = 1; a(); b(); return counter }
   func main(): int sync {
       println(run(() => { counter = counter + 1 }, () => { counter = counter + 10 })); return 0
   }
   ```
   **读数是 `10`，对的数是 `12`——今天给出错的数。** 第一次调用没有执行，且 `counter = 1` 那一版也丢了。

**判据**：**收窄的每一步都要能说"还剩谁在集合里"；说不出名单的收窄，等于换了一个猜。**

## 3. 下一页：按函数值类型里的资源收窄，需要什么

**类型里今天有资源集**：

- `include/camel/core/type/composite/func.h:179` —— `FunctionType::resources() const` 返回 `const sem::ResourceSignature &`（`setResources` 在同文件 `:65`）。
- `include/camel/core/resource_semantics.h:190` —— `ResourceSignature`：`mode`（`:191`，`Generic` 表示"什么都不承诺"、`Concrete` 是声明或推断出的集合）、`params`（`:214`，每个形参一条 `ParamUse{index, cls, name, mode}`，见 `:193`）、`captures`（`:216`）、`ambient`（`:218`）、`anyOf`（`:220`）。

**缺的是把签名里的资源映射回这一层的单元**：

- `param` 好用：调用点知道实参在第几个普通形参位置，`ParamUse::index` 对得上，`cls == Variable` 加 `mode` 就能说出"这次调用写的是第 k 个实参那个单元"，而不是"程序里某个写过这个名字的函数"。
- **`captures` 不好用**：它只有位置（`std::vector<uint8_t>`，按捕获位置），**不带名字、不带作用域**。闭包捕获的是调用点外层的单元，而调用点这一层只有"这个函数值"和它的类型，没有一个把"捕获位置"翻回"哪个单元"的映射。这是这一步真正的缺口——要么让推断出的闭包类型记下捕获的名字（编译期它明明知道），要么在调用点把函数值与它的捕获列表一起留着。
- 旁路（"被调者是本层能解析到图的函数值就用它的捕获表"）**今天给不出任何收窄**：取样 5 个程序（含 `resources/effects.cml` 的 12 个、`two_references_write_one_object_unsynced.cml` 的 1 个）共 **16 个未知调用点，能解析到图的 0 个**。原因是未知路径上被调者只是形参的引用（`DREF`），不是 `FunctionData` 节点，`functionValueGraphOf`（`src/compile/gir/builder_calls.cpp:101`）只能从后者取出图；捕获表本身是有的（`include/camel/runtime/graph.h:656` 的 `captureNames()`），但那条路径取不到图。`inc()` 那种 `let inc = () => ...; inc()` 的调用**根本不走未知路径**（构建时就有 `targetGraph`），所以也不能靠它收窄。

## 4. 判据

**收窄之前先量"能收多少"；量不出来就不收，因为保守的代价是可以算的，收错的代价是算错的数。**

本轮据此做的两笔：形参那笔先量出"5 对里 2 对是两个函数的同名形参"才动；局部那笔先量出"2 对写自己声明的名字里只有 1 对被闭包捕获"，并确认捕获的那一对靠闭包自己留在集合里（否则验收会掉回 `1`）。