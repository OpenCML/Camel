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

2. **调用之后才第一次捕获的变量，按书写顺序当"之后"**（`claimedWriters_`）。**已修**（`a23530ba1`）：读写在调用之后才写下的闭包里时，闭包捕获到的现在是 `:var/after(cell, call)`。
   ```camel
   var counter = 1
   func run(f: () => void): int { f(); let read = () => counter; return read() }
   func main(): int sync { println(run(() => { counter = 5 })); return 0 }
   ```
   **修前读数是 `1`，修后 `5`**（对的数是 `5`）。

3. **两次相邻的未知调用，前一次本身也是未知调用修饰时，不串 `writeBeforeCall_`。** **已修**（`66cfa1303`）：第二次调用的"之前"现在取第一次调用的 `:var/after`，链不再断。
   ```camel
   var counter = 0
   func run(a: () => void, b: () => void): int { counter = 1; a(); b(); return counter }
   func main(): int sync {
       println(run(() => { counter = counter + 1 }, () => { counter = counter + 10 })); return 0
   }
   ```
   **修前读数是 `10`（第一次调用没执行，`counter = 1` 那一版也丢了），修后 `12`**（对的数是 `12`）。

**判据**：**收窄的每一步都要能说"还剩谁在集合里"；说不出名单的收窄，等于换了一个猜。**

**判据**：**边界只有带读数才叫边界；`0/16` 这种读数直接否掉一条设计，省下的不是时间，是一次错的收窄。**（旁路那条就是：量出 0/16，设计当场作废，没写一行代码。）

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

**一个保守集合的价值，最终要看它有没有把错的数变成对的数；`10 → 12` 比任何宽度描述都有说服力。**

## 5. 现在剩下什么

**集合的形状**（`test/cases/resources/effects.cml`，实跑）：`1 name / 1 pair`（`c`，只由闭包 `inc` 写）。形参与局部两笔收窄之后，这个取样里已经**没有可再收的浪费**；因此"按类型收窄"今天不该动，等 `edges` 把 11 条红清完再说。

**三条边界的修后读数**（`std::nvm`，四个后端同数）：

| 边界 | 对的数 | 修后 | 修它的提交 |
| --- | --- | --- | --- |
| 只经 `var` 实参写 | `3` | `3` | 未改（今天靠交出单元，不是靠集合） |
| 调用后首次捕获 | `5` | `5` | `a23530ba1` |
| 两次相邻未知调用 | `12` | `12` | `66cfa1303` |

**那个"未测"的格子现在有结论：无法构造最小程序。** 理由不是构造不出来，是**这条边界在这里不吃数**：`var` 实参把单元交出去，被调者写的就是同一个单元，读一定看见。实测 `func run(a: (var x: int) => void): int { a(counter); return counter }` 配 `(var y) => { y = 5 }`：**`std::nvm` 打 `5`，`std::prl` 也打 `5`**，和语义应得的一样。要让这个形状给出错的数，得让读拿一份"调用之前的拷贝"——这门语言在这里不做拷贝，所以最小程序不存在；能看见它的是**并行执行下的顺序**（谁先谁后），那是 `edges` 的账，不是这一格的数。

**与 `edges` 交叉的一处（只查不改，结论已定）**："被调者体内写它自己的 `var` 形参"（`bump(acc)`）的调用里，`:var/after` 的第一个实参是 `:var/new`。**这不是缺陷**：那条调用之前没有任何写，`:var/new` 就是"调用之前那一版"——当前版本下它就是正确写法。看起来的"裸单元"在有一次前写时已经由 `66cfa1303` 的链解决（`variables.cml` 里 `:var/after → :var/after → :var/new`）。查过的手脚：`builder_operators.cpp:4307` 的两参重载固定 `throughCallee = false`（`builder.h:2255`），填 `writeBeforeCall_` 只在 `throughCallee` 为真时进（`builder_expr.cpp:620`）；按"实参是 `var` 就走 `true`"改过一次，**四条读数里第三条不成立，已回退**——那处改在这两个程序里是空操作（`modifierOf(cell)` 没有值）。

**修好的两个数都钉成了用例**：`test/cases/effects/two_calls_through_values_keep_both_writes.cml`（`12`）与 `test/cases/effects/value_made_after_a_call_captures_its_version.cml`（`5`），注释写明修前分别是 `10` 和 `1`，`callback.*` 那两条在 `test/plans/feat/compile/effects_syntax.plan.toml` 里跑四个后端。

**判据**：**修好一个数之后，第一件事是把它钉成用例；不然它不是被修好了，只是这次对了。**

## 6. 资源账上的那一格：`:var/after` 被数了两次

**读数**：`test/cases/resources/effects.cml` 上，账里是
`RESOURCES node main 32 :var/after: read variable (node 2); result same 0`，紧接着
`RESOURCES node main 33 :var/get: read variable (node 32); result same 0`。node 32 就是 node 2，**同一格被数了两次**。

**那句 `read` 来自声明，不是 footprint 里另加的**：`src/builtin/executors/builtin_semantics.cpp:346-350` 的 `.accesses = {cell(AccessMode::Read)}`；`src/runtime/resources.cpp:783 resourcesOf` 只是照声明算。

**去掉那条 access 会被 check 5 挡（实测）**：只留 `.result = Same(0)` 后跑 `modules.resources`，五条红，报文 `resources: an operator that is not pure touches, allocates or affects something (declare it pure instead)`——`:var/after` 的 traits 是 `stateOnly = true`（`src/builtin/executors/builtin.cpp:671`），检查在 `src/core/operator_table.cpp:552`。已回退。

**访问种类只有三种，没有"别名/同一份值"这一类**：

- `include/camel/core/resource_semantics.h:118` —— `enum class AccessMode : uint8_t { Read, Write, ReadModifyWrite };`（`accessModeName` 在 `:120`，`writes()` 在 `:123`，`uniteAccess` 在 `:129`）
- `include/camel/core/resource_semantics.h:131` —— `struct ResourceAccess { Resource resource; ResourceBinding binding; ResourceRegion region; AccessMode mode; }`（`mode` 在 `:135`）
- 声明里的 `cell(mode)` 就是 `operandAccess(ResourceClass::Variable, 0, mode)`（`src/builtin/executors/builtin_semantics.cpp:322`）
- "别名"这一层只存在于**结果**：`ResultAlias::Kind`（`:142-149`：`Fresh` / `Same` / `View` / `MayAlias` / `Unknown`），**不在访问上**

**结论**：要让 `:var/after` 在账上算"同一个名字"而不是"再读一次"，**需要新增一种访问种类**（例如"它交出的就是第 k 个操作数，不另算一次访问"）。那是语言层的决定，本线不自行加。

**判据**：**账上把同一格数两次，会让两条本来无关的红同时出现（`core` 与 `source.declared_resources`）；一个红数到两条，先怀疑账本重复计数，而不是两个缺陷。**

## 7. 合上 `lane-edges` 之后的那四条红（读数）

合 `lane-edges`（`6a6fccfaf`，只合、未推）之后整闸 **total 8045, pass 8032, fail 4, skip 7, known 2**，四条正是 `core`、`poison:modules.resources:core`、`source.declared_resources`、`poison:compile.effects_syntax:source.declared_resources`。两族各一条根子：

**`core` 族：节点号整体 +1，因为 `main` 的 `CALL` 节点现在记了一条访问。** 计划期望 `RESOURCES node main 19 :var/take: rmw variable (node 18); result may_alias 0`，实际是 `RESOURCES node main 20 :var/take: rmw variable (node 19); result may_alias 0`；账里多出一行 `RESOURCES node main 15 CALL: rmw variable (node 2)`，而计划的注释（`test/plans/feat/modules/resources.plan.toml:80-81`）写的是"The `CALL` node of `main` records no access of its own"。访问从 `Resolver::call` 的签名/足迹分支出来（`src/runtime/resources.cpp:444`，签名在 `:485-489`，操作数绑定在 `:519-526`）。

**`source.declared_resources` 族：两次 `bump(cell)` 现在指两个不同的版本节点。** 期望（`test/plans/feat/compile/effects_syntax.plan.toml:33-44`）：两次调用**指同一格**且**按 control 定序**；实际是
`RESOURCES node main 8 FUNC bump: rmw variable (node 7)` 与 `RESOURCES node main 9 FUNC bump: rmw variable (node 10)`，其中 `node 10 :var/after: read variable (node 7); result same 0`，配对只剩 `order main 8 9 resource ordered by data`。也就是说：`:var/after` 作为"同一格的下一版"，在账上被当成了**另一个值**。

**它比的是哪两样**：`RESOURCES node`/`RESOURCES order` 这些行（由声明 + 建好的图算出来，`src/runtime/resources.cpp:783 resourcesOf`）对**源码程序里写的 clause**（`test/cases/effects/declared.cml:20-32`），断言写在计划的内联 `verify_script` 里（`test/plans/feat/compile/effects_syntax.plan.toml:26-53`）；**没有快照文件**。

**判据**：**结果说"就是那个单元"、访问却说"读了它"——两者同时成立时，账本迟早会数两次；先看哪一句是多余的，再决定改哪一边。**

## 8. 重录：旧账与新账，两份都贴

方法：同一份代码，合 `lane-edges` 前后各跑一次 `test::resources`，把 `RESOURCES node`/`RESOURCES order` 原样留下（`14d1052a8` 与合后 `692c053d1`）。**两段账都能解释，才叫"形状变了"。**

**`test/cases/effects/declared.cml`（两次 `bump(cell)`）**

旧账（`14d1052a8`）：
```
RESOURCES node main 8 FUNC bump: rmw variable (node 7)
RESOURCES node main 9 FUNC bump: rmw variable (node 7)
RESOURCES order main 8 9 resource ordered by control
RESOURCES node main 13 :var/get: read variable (node 7); result may_alias 0
```

新账（`692c053d1`）：
```
RESOURCES node main 8 FUNC bump: rmw variable (node 7)
RESOURCES node main 9 FUNC bump: rmw variable (node 10)
RESOURCES node main 10 :var/after: read variable (node 7); result same 0
RESOURCES order main 8 9 resource ordered by data
RESOURCES node main 14 :var/after: read variable (node 7); result same 0
RESOURCES node main 15 :var/get: read variable (node 14); result may_alias 0
```

读法：第二次调用的实参现在是第一次调用产出的那个版本（`:var/after`，`Same(0)`，**同一个格子**），于是两次调用之间有了 data 边——**定序种类从 control 变成 data，不是断言放宽**。计划里 `pair('bump', cells, 'data')` 加一句"第二次的节点要么是同一格，要么是第一格上的 `:var/after`"，把这条形状写死。

**`test/cases/resources/effects.cml`（`core` 那几条）**：旧 `19 :var/take: rmw variable (node 18)` → 新 `20 :var/take: rmw variable (node 19)`；旧 `order main 12 35 ... by control` → 新 `order main 12 37 ... by data`；旧 `order main 19 21 ... by data` → 新 `20 22 ... by data`；账里多出 `16 :var/after: read variable (node 2)`。**这一段没有重录**，因为它重录后会撞上计划里一条全局断言，见下。

**停下来的那一处（按"必须删就停"）**：按新账重录 `core` 之后，`assert(!out.includes(' unordered'), 'the program orders every conflicting pair')`（`test/plans/feat/modules/resources.plan.toml:149`）会红——新账里有一对 **`RESOURCES order main 15 16 resource unordered`**：`main` 的 `CALL` 节点 15 写 node 2，它上面那个 `:var/after` 16 也指 node 2，两者是冲突却**没有顺序**。这不是重录能解决的（删断言 = 删既有行），所以这一段退回旧账、报上去。

**判据**：**重录可以是"形状变了"，不能是"数变松了"；判据是同一份代码在合入前后各跑一次，两段账都能解释。**

## 9. 两边读数相反时

同一对节点 `15`/`16`，`edges` 报图上已有 `N0F [:var/after] -> N0E [ORDER]`；我在合并后的同一棵树上量到的是：`main` 的 ORDER 边只有 `8 -> 15` 和 `10 -> 15`，而 `N0F` 在这棵树里是 `CALL 15`、不是 `:var/after`。两边都在实测，所以先对齐号：`std::gir` 打的是十六进制节点号，账本 `RESOURCES node` 打的是十进制序号，**不是同一套号**；对齐之后才谈谁对。

读数（`effects.cml`，`main`，`15` = `CALL: rmw variable (node 2)`，`16` = `:var/after: read variable (node 2); result same 0`）：

| 前置条件 | 读数 |
| --- | --- |
| 同图？ | 是 |
| 两个方向有 data 路径？ | `15→16` 否，`16→15` 否 |
| 两端都吃 order？ | `15` 有 2 条 order 前驱（`8`、`10`），`16` **0 条**；`16` 的 ctrl / sync 前驱也各为 0 |

**判据**：**两个人报同一件事的相反读数时，先对齐是哪个用例、哪个节点编号，再谈谁对。**

### 不是"过期读"，是账本把版本折回了格子

同一对的第三个问题：读到的版本是不是比写旧。读数（`effects.cml`/`main`，行号即程序顺序）：

| 节点 | 账 | 对应源码 |
| --- | --- | --- |
| `9` | `:op/assn_l: write variable (node 2)` | 24 行 `c = c + 2` |
| `16` | `:var/after: read variable (node 9)` | 25 行 `bump(c)` 之后的那个版本 |
| `15` | `CALL: rmw variable (node 2)` | 28 行 `inc()` |
| `33` | `:var/after: read variable (node 16)`（第二操作数是 `15`） | 28 行之后的那个版本 |
| `34` | `:var/get: read variable (node 33)` | 34 行 `println(c)` |

两件事：**`16` 在程序顺序上早于 `15`**（它记的是 `bump` 之后那个版本，25 行；`15` 是 28 行的调用）；**真要看到 `15` 那次写的读是 `33`/`34`，而它确实接在 `15` 上**。所以没有"读到过期版本"的缺陷——`16` 读的版本 `9` 是不可变的，之后那次写覆盖不到它，它本来就不该和 `15` 有序。账本把 `9` 与 `2` 折成同一资源（`Resolver::originOf`、`Resolver::mayAlias`），才把它们配成一对并要求顺序。

**判据**：**一对没被排序的访问，先问它们该不该有序；若读到的版本比写旧，那不是缺一条边，是读错了版本。**

## 10. 这条口径要动多大：把快层的账全量量一遍

口径指的是：`Resolver::originOf` 把一个版本节点折回它所属的格子，于是"读这个版本"和"写这个格子"被配成一对冲突。要动它，得先知道现在有多少对靠这个折叠才成立。

方法：快层里所有会打资源账的用例（`cases/resources/effects.cml`、`cases/effects/declared.cml`、`cases/modules/io/ordering.cml`、`cases/resources/conflict_per_resource.cml`、`cases/resources/names.cml`，共 6 个图）跑 `test::resources`，把每条 `RESOURCES order` 的两端追到格子（`:var/after`、`:op/assn_l` 一路追到 `:var/new`），按"两端是不是同一个格子、值是不是同一个版本"分类。

| 形状 | 对数 |
| --- | --- |
| 两端不同资源（`stdout`、对象、句柄、ambient） | 38 |
| 同一格子、**同一版本**（真冲突） | 4 |
| 同一格子、**不同版本**（读版本 + 写格子） | 9 |

其中**没有顺序的只有 1 对**，正是 `main` 的 `15 16`（`CALL` 写格子 `2` 对 `:var/after` 读版本 `9`），形状是"不同版本"。**"同一版本上的两个真冲突"里没有一对是没排过序的**——4 对全部有序。

所以这条口径的代价是 **9 对**（8 对已有序 + 1 对没序）：不折叠它们，这 9 对不再进账本，`15 16` 那条 `unordered` 随之消失。

**期望这一侧**：全量计划里写死 `ordered by control` 的有 **8 行**（`resources.plan.toml` 7 行、`effects_syntax.plan.toml` 1 行），写死 `ordered by data` 的 **7 行**。这 8 条控制序对应的对，按上面的分类**全部落在"两端不同资源"或"同一版本"**，**没有一条是"不同版本"那 9 对**——所以改口径**不牵动任何一条 `ordered by control` 期望**；会变的只有那 9 对所在的期望行（其中一半还没重录、仍写着旧号）。

**判据**：**改一条口径之前，先量它会牵动多少条期望——代价是裁决的一半。**

## 11. 折回格子漏了一种算子

`Resolver::originOf` 本来只跟 `Same(k)` / `View(k)` 往后折，`may_alias(k)` 不算——可 `:var/get` 和 `:var/take` 的实参就是它读/搬的那个东西（格子或它的某个版本），它们的 `result may_alias 0` 是给优化器的"不保证同一对象"，对账本却是同一对象。补上这两种算子的折返之后：

- `:var/take` 的实参不再停在一个值节点上，`short` 的 `rmw object (node …)` 折到了那个格子。
- `array:sort_l_in_place` 落到具体格子上，`main` 的 footprint 里的 **`rmw object (any)` 消失**（原来"可能有任意对象"现在说得出来是哪一个）。
- **`unordered` 的条数没变**：改前 1 条（`main 15 16`），改后仍是 1 条。这条折叠不是那对 `unordered` 的解药，是另一处的漏写。
- `core` 的报错从 `missing: RESOURCES node main 19 :var/take: rmw variable (node 18); result may_alias 0` 变成 `missing: RESOURCES footprint main … rmw object (any); external`——红法换了，原因是 footprint 少了一项、期望还写着它。

**判据**：**折回格子漏了一种算子，看起来像"口径不对"；先把折返补全，再谈口径——否则会把一个漏写的分支说成一次设计冲突。**

重录只动那一行（`main` 的 footprint）：

```
改前  RESOURCES footprint main rmw ambient rng; write ambient stdout; read ambient fs; rmw object (any); external
改后  RESOURCES footprint main rmw ambient rng; write ambient stdout; read ambient fs; external
```

"精度变好"就是这一项：`rmw object (any)`（"可能碰到某个对象"）变成了具体格子，于是 footprint 不再需要"任意对象"那一条。重录之后 `core` 回到它原来的红法——`missing: RESOURCES node main 19 :var/take: rmw variable (node 18); result may_alias 0`，也就是那对 `unordered` 挡住的那半，等作者裁。`unordered` 仍是 **1**。

**判据**：**折返补全之后红法变了，说明它改的是精度不是口径——精度变好要重录期望，口径之争才等裁定。**