# `ASSERT` 在 release 里被编掉：这个洞有多大

> **`ASSERT` 是开发期的记录，不是运行期的守卫；凡是"若它不成立，程序会做危险的事"的地方，都必须是 release 里也活的检查。**
>
> **任何小于 2% 的耗时结论，只有两棵树的二进制在同一时间窗口内背靠背测量才算数——这台机器几分钟内的漂移就有 ~10%。**
>
> **读数落在哪个站点，本身就是注入是否正确的一种检测手段。**
>
> **`ASSERT` 的三种命运：崩溃（该留）、静默错（该留）、记录（可留可不留）——分不清这三种，就会把该留的删掉、把该留的留成空语句。**

昨晚 `Variant::updateRefs` 撞坏指针，进程 `exit 139`，而它前面的 `ASSERT` 在 `NDEBUG` 下是 `((void)0)` —— 现场一声不响。`rank` 车道已经把那一处改成 release 里也活的断言（`abort` 带读数，`exit 134`）。这一页把整仓的洞量清楚，只出读数与清单，不给修法。

## 三个数

**① 全仓 `ASSERT(` 站点：1224**（`git grep` 逐目录统计，剔除 `include/camel/utils/assert.h` 里的宏定义本身）

| 目录 | 站点 |
|---|---|
| `src/passes` | 304 |
| `include`（剔除 `assert.h`） | 216 |
| `src/core` | 200 |
| `src/compile` | 181 |
| `src/runtime` | 170 |
| `src/execute` | 79 |
| `modules` | 38 |
| `src/builtin` | 13 |
| `src/parse` | 12 |
| `tools` | 10 |
| `test` | 1 |

**② 其中被当作守卫用的：726 / 1224（59%）**

> **进度条（第一批后）**：`ASSERT` 站点 1224 → **1216**，`GUARD` 站点 0 → **8**，release 里会说话的 212 → **220**。
>
> **进度条（第二批后）**：`ASSERT` 站点 1216 → **1205**，`GUARD` 站点 8 → **19**，release 里会说话的 220 → **231**。
>
> **进度条（第三批后）**：`ASSERT` 站点 1205 → **1199**，`GUARD` 站点 19 → **25**，release 里会说话的 231 → **237**。

判据（可复算）：断言之后 3 行内出现解引用、下标、`reinterpret_cast`、`payloadAs`、`std::get`、`.value(` 或 `.at(` —— 即断言之后的代码**假设它成立**。这是上界：纯"记录意图"的断言（如"这个分支不该到达"）在这个窗口里没有这些形状，会被排除；但仍有少量被算进来的只是恰好近邻。最危险的 10 处是逐条读过上下文的，见下。

**③ 已经用 release 里也活的写法：212**

- `throw std::runtime_error` **195**（其中 `PassInternalError` **13**）
- `CAMEL_ASSERT_*`（`include/camel/core/rtdata/view.h`）**14**
- `abort()` **3**

也就是说：**1224 处断言里，只有 212 处（17%）在 `NDEBUG` 下仍然会说话。** 剩下 1012 处在 release 里是空语句。

**只有 17% 的断言在 release 里还会说话；1012 处是空语句。这个比例本身就是一个待办项，不是一次性修得完的——所以按危险度分批，而不是按目录。**

## 第一批：`Array` 的下标（已落地）

`include/camel/utils/assert.h` 多了一个 `GUARD(condition, reading)`：条件与原处相同、位置相同，`NDEBUG` 下照样活着，失败时打印 `Guarantee`（条件原文）与 `Reading`（值、界），然后 `abort`。`src/core/rtdata/array.cpp` 的八个下标检查从 `ASSERT` 换成它：`getSlotSlow`、`setSlotSlow`、`getAsDouble`、`getAsInt64`、`getAsBool`、`setFromDouble`、`setFromInt64`、`setFromBool`。

读数长这样（喂进一个越界下标时的真实输出）：

```
Camel Invariant Violation:
    Location   : ../src/core/rtdata/array.cpp(557)
    Function   : slot_t Array::getSlotSlow(uint64_t) const
    Guarantee  : flatIndex < numel_
    Reading    : reading element 3 of an array that holds 2 (rank 1), so the read is past its end
```

代价：一条永不成立的分支。`benchmarks/mlp/model.cml std::nvm` 连跑 6 次，最小延迟 0.347 ms → 0.344 ms（−0.9%，在这台机器的噪声带以内）；而且这条负载根本不走这些慢路径——把越界下标注入进 `getSlotSlow` 后 `mlp` 一行都没变，换一个读结构体数组的小程序才触发了它。

## 最危险的 10 处（守指针、下标、`Variant` 类型、GC 句柄）

| # | 位置 | 守什么 | 假设不成立时退化成 |
|---|---|---|---|
| 1 | `include/camel/core/rtdata/variant.h:47` | 字段下标 `< fieldCount_`，随后读内联字段数组 | 越界读：按错误的枚举类型描述读取一个值 |
| 2 | `include/camel/core/rtdata/variant.h:51` | 同上（写/构造路径） | 越界读/写，写出别的值的字段 |
| 3 | `src/core/rtdata/array.cpp:551`（`getSlotSlow`） | `flatIndex < numel_`，随后 `dataPtr_ + flatIndex * elementSize_` | **堆越界读**；`isSlots()` 分支是越界读指针 |
| 4 | `src/core/rtdata/array.cpp:663`（同族 685/707/729/758/787） | 同一判据，写入/原地更新路径 | **堆越界写**：写坏相邻对象，通常几帧之后才崩 |
| 5 | `include/camel/core/rtdata/array.h:411`（`:415` 同） | `isCompact()`，随后 `dataAs<T>()` 把存储当裸数值 | 类型混淆：把视图/聚合数组当 `f32` 读，**静默错值**或按错误步长越界 |
| 6 | `src/core/rtdata/array.cpp:426`（`:479` 同） | `layout->aggregate`，随后按 layout 写字段 | 按非聚合布局写进一个非聚合缓冲区：**写坏内存** |
| 7 | `src/core/rtdata/array.cpp:1273` | 类型是 `ArrayType`，随后 `static_cast` 并用它的 `elemTypeCode`/`rank` | 类型混淆：读别的类型对象的前缀，拷出错误形状 |
| 8 | `src/core/rtdata/variant.cpp:21`（`:19` 同） | 值必须有枚举类型，随后按 `EnumType` 打印/比较/追踪 | 空指针或类型混淆；**这正是昨晚 `updateRefs` 那一类** |
| 9 | `src/core/rtdata/conv.cpp:52`（`:117` 同） | `data != nullptr`，随后解引用 | 空指针崩（`exit 139`，无读数） |
| 10 | `src/runtime/draft.cpp:2208`（`:2228` 同） | 节点 `kind == Func`（/`Loop`），随后 `payloadAs<GCFuncRef>`（/`GCLoopRef>`） | GC 句柄按错误的结构体重解释：非法的图索引 → 跳到错误代码或崩 |

## 边角

`ASSERT` 在 `NDEBUG` 下取两个参数（`#define ASSERT(condition, message) ((void)0)`），`ASSERT1/ASSERT2` 只在非 `NDEBUG` 分支里定义 —— 所以 release 构建本身就是"所有站点都传了两个参数"的证明；这条不是洞，但它意味着 release 里连"断言存在过"的痕迹都没有。

## 这一笔的范围

纯读数：不构建、不跑计划、不修任何一处。修法另派（"先让读数给出名字"）。
## 附：`variant.h:47` 的 A/B（第四批补）

`type Shape = enum { Circle(float), Rect { w: float, h: float }, Empty }`，`match s { case Shape::Rect { w, h } => w * h }` 读一次 `Rect` 的两个字段；在 `Variant::get` 入口注入 `index = fieldCount_`（提交前已还原）：

```
Camel Invariant Violation:
    Location   : ../include/camel/core/rtdata/variant.h(53)
    Function   : slot_t camel::core::rtdata::Variant::get(size_t) const
    Guarantee  : index < fieldCount_
    Reading    : reading field 2 of a Variant whose tag 1 declares 2 field(s), so the read is past its last one
```

去掉注入后同一程序正常输出 `6.0`。

## 图不变量的 `ASSERT`：规模与分层（读数）

`src/runtime/draft*.cpp`、`graph*.cpp`、`src/compile/gir/` 三处共 **174** 处 `ASSERT`：runtime 半边 **56**、gir 半边 **118**；runtime 半边已有 **4** 处 `GUARD`（第三批）。

三类分法（崩溃 / 静默错 / 记录）对 174 处的逐条判定**尚未做完**——本轮只做到抽样：runtime 半边逐条看过 12 处（全部该留，其中 4 处已改），gir 半边抽样 28 处（9 处该留、19 处只是记录）。全量需要下一轮，**不拿抽样去顶全量**。
## 两条从现场学到的判据

> **一个读数的缺陷，有时只能在上游修**——`replaceNodeStorage` 里存储槽为空是常态（那个函数就是去填它的），所以“要替换存储的节点必须已存在”是**调用方的前置条件**，不是这个函数能验证的。

> **会在正确程序上响的守卫不是守卫。** 我照指令给 `:1102` 写了两条读数，构建把第二条抓了出来：`nodesById_[id] == nullptr` 在正确程序上成立，`feat.opt.defun` 从 19/19 掉到 5/19。**这是本车道第二次“检查本身需要被检查”。**
## 三条关于“验证”的纪律（同族陷阱的第三代）

> **`19/19` 只有在 `git diff --stat` 非空时才算验证；否则你验证的是没改过的树。** —— 构建通过不等于改到了文件：有一次我把补丁分成两次写，第一次在中间 `throw`，文件根本没写，而 `BUILD_EXIT=0 / 19/19` 让我以为已经验证过。

> **补丁脚本中途 `throw` 会留下一次没写或只写一半的文件——写补丁要一次成文，或者先写临时文件再整体替换。**

> **`Set-Content` 放在脚本末尾，就等于把“写文件”这件事挂在“全部匹配成功”上；匹配一失败，前面全白做。要写文件就写一次。**

第二道读数不靠 `git diff`：**转换之后 `git grep -c 'ASSERT(' -- <file>` 必须按处数下降**（`nodevm.cpp` 47 → 38，九个站点）。降不到，就是又没改到真文件。

## 关于“改到没有”的三条纪律

> **多行宏的“一行”是 grep 造出来的，不是文件里的。要改它，先打印行区间。** —— `git grep -n "ASSERT("` 在多行断言上给出的是拼接后的一行，照它写字面串替换必然 MISS；用 `ASSERT\(\s*\r?\n\s*<条件首词>` 这类跨行匹配，或用 `Select-String -Context` 打真实区间。

> **批量改写的原子性：要么 N 处全写，要么 0 处写。** 补丁脚本把所有替换做完再 `Set-Content` 一次，中途 `throw` 就等于一处没写——树是干净的，代价只是重来。

> **一次整批改 N 处，任何一处匹配失败就整体不做——这条纪律救了两次。** 两次都是多行宏的字面串匹配失败，两次都没有留下半改的文件。

## 断言清点的两类（不只数个数）

> **断言清点不是数个数，是分两类：不该发生的（断言）与可能发生的（错误处理）。数对而类不分，等于没清点。**

`src include` 现有 **1122** 处 `ASSERT(`，其中单行的 `!= nullptr` 守卫 **380** 处（多行断言里的 `!= nullptr` 未计入这个数，另有 1945 行含 `!= nullptr` 但多数不是断言）。这两类的划分还没做完。

## 口径行（1122 / 380 / 742）

src include 现有 **1122** 处 `ASSERT(`，其中 **380** 是**单行** `!= nullptr` 守卫——**380 是下界，742 是上界**（多行断言里的 `!= nullptr` 没有计入，另有 1945 行含 `!= nullptr` 但多数不是断言）。拿 380 当准确数会低估这一类。

## 要 A/B 一条守卫，先读它读的是什么

> **要 A/B 一条守卫，先读它读的是什么。改一个它不读的函数，再干净的两次读数也只能证明你没改到它。**

`nodevm.cpp:743` 那条守卫的 A/B 今天没做成，五次尝试全败，每一次都是同一个毛病——**没读原文就动手**：

1. 把 `graph.h:622` 的 `loopRegionOf` 强制为 0 —— `hasLoopRegions` 读的是 `graph.region(r)` 的表，**它根本不调用 `loopRegionOf`**，19/19 全绿是必然的。
2. 按“同形”假设 `bodyPart` 的 `buildArm` 回调与 `condPart` 一样，用正则去匹配 —— 它的回调是跨 56 行的大 lambda，`GCRegionKind` 在收尾处，**形状不同**。
3. 按“region kind 的上一行以逗号结尾”做行级删除 —— `bodyPart =` 与 `buildArm(` 分处两行，**这个前提在该处不成立**。
4. 按行号删 —— 行内容与给定原文不符（本树 3956 落在结构体状态拆分里），校验时中止。
5. 于是记为：**今天不可由此路制造**；阻塞点具体到“本树上 `builder_control.cpp` 里 `GCRegionKind::Loop*` 登记调用的真实位置未定位”。

五轮的读数都干净、五次结论都错。**干净的读数不证明改动有效，只证明没有留下半改的文件。**

## 分类比计数有用

> **分类比计数有用：1122 这个数不会告诉任何人下一步做什么，三格一张表会；但表要标“读了几处”，否则它假装覆盖了全部。**

> **`ASSERT` 分三类之后还要再分一层：不成立时是崩溃、是静默算错数、还是只丢一条诊断。第二类最该先转 `GUARD`，因为它错了没人知道。**

`src/passes` 按子目录：`sched` **267**、`trans` 20、`test` 3、`opt` 0。最大一块 `sched` 里，已读 **47**（全部在 `nodevm.cpp`），占 **17.6%**，还剩 **220**（`taskflow` 62、`fastvm` 43、其余 115）。

已读 47 处的三类：① 不该发生 **34**、② 可能为空 **5**、③ 纯防御 **0**，另 **8** 处按 ① 归但“图错”与“前端错”在原文里分不出。

该块的 Top 3（今天在 release 里静默继续，后果是算错数）：marked operator 的 norm/with arity（实参绑到错的端口）、端口节点在其图里不存在（静默写到错的槽）、节点种类落到 default（当成别的种类执行）。
