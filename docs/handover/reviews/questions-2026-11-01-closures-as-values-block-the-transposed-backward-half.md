# 库层 `vjp` 的最后一堵墙：`split` 的后一半把闭包当值搬，而转置没有地方放它

> **问什么**：`transpose` 要转置 `split` 的后一半，但那张图里**没有一张有名字的图可以被转置**——
> 它有一串「闭包当值」的调用。要往下走，必须先决定**闭包能不能作为值进转置图**。
> **堵着谁**：M4 的 `vjp` 组装。今天 `transpose(split 后一半)((3.0, 2.0), 1.0)` 拿不到 `(12.0, 9.0)`。
> **为什么我不自己定**：这正是你那条 standing 裁定（**去函数化不许留一部分族照原样写**）
> 覆盖的范围，而它现在真的挡住了主线的最后一步。

## 图长什么样（实测，逐字）

`f(x, y) = x * x * y` 的 `split` 后一半，12 个节点、**零个 `Func`**：

```
0:Port(残差捆) 1:Port 2:Port 3:Data 4:Accs 5:Accs 6:Oper 7:Call 8:Data 9:Call 10:Call 11:Call
```

| 节点 | kind | 类型 | 输入 | 被调值的生产者 |
|---|---|---|---|---|
| 7 | `Call` | `(dx,dw)=>float` | norm=`[6,4]` **with=`[3]`** | **Data 3**：存成静态值的闭包，类型 `(x,w)=>((dx,dw)=>float)` |
| 9 | `Call` | `(dx,dw)=>float` | norm=`[5,5]` **with=`[8]`** | **Data 8**：同类型闭包，另一个静态值 |
| 10 | `Call` | `float` | norm=`[1,1]` **with=`[9]`** | **Call 9**：**调用返回**的闭包（部分应用） |
| 11 | `Call` | `float` | norm=`[10,2]` **with=`[7]`** | **Call 7**：7 号返回的闭包 |

**清单一句话**：2 个静态闭包 `Data` + 4 次穿过闭包调用（`with=[3]` / `with=[8]`）+ 1 次调用返回的闭包（`with=[9]` / `with=[7]`）。

今天转置到这里停下，拒绝指到生产者：
```
TRANSPOSE function=split_of_jvp_of_f_backward skip=call node=11 callee=none value=10:8 with=7:8
```

## 已经走过的两步（都不是这堵墙）

- **`transpose` 收残差**：类型那半通了（认出「第一参数是全 `float` 的元组 = 残差捆」），端口、`accs(捆, i)`、
  `mul_t`/`div_t` 的「常数直读、否则走残差」都在位。
- **`transpose` 转置一个直接调用**：通了。`g(x)=2.0*x`、`f(x)=g(x)+g(x)` 读 `transpose(f)(1.0,1.0) = 4.0`（手算 2+2），
  `skip=kind Call` 这一条已从清单里消失，`transposeRuleOf` 的调用者数**仍是 2**（区域与调用只有一种走法）。
- **卡住的是第三步**：被调值本身**不是有名字的图**，而是 `Data` 里的闭包、或另一个 `Call` 的返回值。

## 三条路，代价与收益

### A（默认建议）— 等去函数化：闭包降成「标签 + 环境」，被调值就有名字

- 你这周的裁定本来就是「**去函数化不许留一部分族照原样写**」；这条路把闭包从值变成数据，
  被调值就落到一个有名字的 tag 上，`transpose` 按 tag 取被调图的转置即可，**不需要装任何闭包运行时表示**。
- 代价：等去函数化那一批落地；`vjp` 的组装在那之前保持 pending。
- 收益：**不需要为转置单独造一条闭包表示**——否则就是两条表示并存，正是那条裁定禁止的。

### B — 转置图里允许「闭包当值」，按运行时被调值分派

- 代价：转置图要能存闭包、能按值分派，**等于给转置单独造一条闭包表示**；
  而 `split` 的后一半每天都会产出这种值，等于把这条表示变成常态。
- 收益：M4 今天就能出 `(12.0, 9.0)`。
- **与 standing 裁定冲突**：这就是「留一部分族照原样写」。

### C — `split` 的后一半不要产出闭包：把四次调用内联成一张有名字的图

- 代价：要 `split` 在做的时候就把被调图拉平（内联），`split` 的语义变成「顺带内联」；
  切两次就会重复内联，图会涨，而且内联与 `std::opt::inline` 是同一件事被做了两遍。
- 收益：后一半是一张平图，转置那半就能一路走到底，读数为 `(12.0, 9.0)`。

## 判据

**改完能删掉什么**：A 什么都不用新增（闭包表示本来就要被去函数化删掉，转置跟着受益）；
B **新增**一条表示，且与裁定正面冲突；C 让 `split` 多背一个内联职责（还要和 `std::opt::inline` 划清边界）。
**没有一种收益是“删行”的**，所以这一页要的是**方向**，不是又一次试错。

## 同一件事的另一堵墙，两条路的价格（实测，附命令）

`split` 的残差位在**编译结束后**才由图定（定型比建图早一个阶段），所以那一页（`questions-2026-11-01-the-residual-slot-has-no-type-until-execution.md`）
摆了两条路。这里把两条路的**价款**量出来：

**甲 —— 把 `jvp`/`split`/`transpose` 挪进编译阶段**（图在建图期就在手里）
- 拼写数：`stdlib` 值级 **4 行**（`stdlib/vjp.cml:44,51,62,65`）+ **3 行文档示例**（`stdlib/signatures/transforms.cml:43,64,79`）；
  `vjp.cml` 要换 **11 行**，两条同形声明并成一条泛型；要改写的程序 **5 个文件**。
- 调用者数：每个算子 **1 个注册点 + 1 个 kernel**（`split_operator.cpp:179/:186`、`jvp_operator.cpp:218/:225`、`transpose_operator.cpp:170/:177`），**外部 C++ 调用点 0**。
- 判据：**能删 342 行**（`split_operator.cpp` 179、`jvp_operator.cpp:81-226` 146、`transforms.cml:54-62,66` 10、`vjp.cml:47-53` 7），
  **但要搬 1720 行**（`split.cpp` 694 + `jvp.cpp` 1026）进一个新 pass。**按“删不掉就是净负”，甲是净负。**
  （复核 2026-11-01：上面的行数是当天的文件量，今天读作 `split_operator.cpp` **209**、`jvp_operator.cpp` **277**、`transforms.cml` **116**、`vjp.cml` **104**，`split.cpp` **1470**、`jvp.cpp` **1147** ——（`1142 → 1147` 是同一笔的收尾：`expandRuleRightSide` 改吃按位次取值的 `valueOf`，调用点自己拒绝越界，多 5 行） 三个 `transforms/*.cpp` 随 `lane-gs6`、`lane-reinterp2` 两笔合并长出来，之后 `split.cpp` 又随 `914e09263`、`e0c206239`（残差元组自成一个类型）多了 96 行、随 `fdb60ba49`（选臂成为残差）多了 437 行，`jvp.cpp` 随 `728b9f27c`、`4fbdc4e3a` 与 `ba0a37be9`、`c98dd492f` 多了 37 行，`split_operator.cpp` 又随 `67509bd1a`（kernel 不再自己定类型）少了 2 行，`jvp_operator.cpp` 与 `jvp.cpp` 又随 `c8bc3d87f`（`jvp(f)` 在调用方的图建起来时实例化）多了 5 行与 32 行，`jvp.cpp` 再随 `lane-remat` 的核声明表多了 19 行（1315→1334），`split_operator.cpp` 再随本车道让切口在构建期回答（`printSplitCallReading` 一条读数）多了 8 行（196→204）、`jvp.cpp` 同时多 6 行（1366→1372，构建期的 `liftedFrom=` 读作 `unassigned`，那里没有运行期的图 id）；此后 `lane-remat` 又把 `jvp.cpp` 的核声明表删掉、改成调用节点上的 `tangentArgs`（1372→1417），`split.cpp` 拷贝调用时把那个计数一起带过去（1466→1470）；`vjp.cml` 随 `07451069e`（拉回一律返回元组、一元写成 `(float,)`）多了 10 行，此前随 `lane-m4-assembly` 的泛型化变短；`jvp.cpp` 最后随 `21aa36805`（右侧展开整段移进 `rule_expand.cpp` 给 `transpose` 用）少了 277 行、多 2 行 include（1417→1142），`transpose.cpp` 随 `dffbbb961` 少 38 行（1889→1851）。命令：`node test/tools/check-doc-readings.mjs`。）

**乙 —— 每次实例化新铸一个刚性类型，并在原则 5.10 上开一条例外**
- 一个 `TypeCode` 落地要动 **7 点**：枚举（`type/base.h:56` + `TypeFlag` `:39`）、`typeCodeToString`（`base.cpp:65`，35 分支且 `:111` 有 `default:` **漏了不报错**）、
  `Type::equals` 与 6 处覆盖、`TypeStorageKind`（`17` 处）、`intern.h:16-22` 的哈希 key、四处序列化、三处运行时值。
- **其中哈希那一点是死结不是成本**：表的 key 是「构造子 + 组件 `TypeId`」，而残差的组件正是此刻还不存在的那个数。
- 调用者数：**1019 个 `case TypeCode::` 分支、73 个文件**；原则 5.10 在代码里被引用 **4** 处、文档 32 行，**执行点只有一个**（`src/runtime/graph_types.cpp:55`）。
  （复核 2026-11-01：现在是 **1024 处 `case TypeCode::`、74 个文件** —— `eeb02d2d4`（`lane-m4-assembly`）带进 `src/core/tangent_space.cpp` 时多出 3 处、1 个文件；原则 5.10 在代码里仍是 **4** 处，文档里 **38** 处（这个数每有一页写到 5.10 就变，不设检查：`check-doc-readings.mjs` 只锁代码里那 4 处）。命令：`node test/tools/check-doc-readings.mjs`。）
- 判据：**例外只许落在 `graph_types.cpp:55` 的一处白名单**，并配一个 ledger（照 `test/tools/check-env-switches.mjs`）锁死构造点恰好 1 处——
  没有它，5.10 的唯一执行点迟早被加第二项，长成第二个 `any`。

## 默认与正在做的事

**按 A 继续**：`vjp` 的组装保持 pending（模块表里 `pending` 那句原因会随每次撞墙更新，编得过那天它自己退出 1），
`transpose` 不再试着装闭包；**同时把 `transpose` 的拒绝信息钉成上面那一行**（指到生产者节点与 kind），
这样去函数化那天，“被调值有没有名字”一读就知道能不能过。

**复核 2026-11-01（`lane-gs6`）——"没有捕获"也过不了，缺的是名字，不是捕获**：把边界再切细一刀，同一天、同一棵树量了两条：

- 带捕获（`g2(x) = let h = (y: float) => y*x; h(2.0)`）：`TRANSPOSE function=g2 skip=call node=4 callee=none value=3:0 with=2:4`；
- 不带捕获（`double(x) = 2x`，`g(x) = let h = double; h(x)`）：`TRANSPOSE function=g skip=call node=2 callee=none value=0:1 with=1:0`。

**两条拒绝落在同一处、同一个原因**：`let h = double` 里的值是一个 DATA 节点、装着一个函数常量（GIR 写作
`Function(graph=double, tupleSlots=0)`），而 `GCGraph::calleeIdOf` 只从 `GCNodeKind::Func` 节点里取图 id——函数常量的图存在静态区、以图级引用记着（`kGraphRefStatic`），没有"这个节点指向哪个图"的取值口。所以**把捕获拿掉并不会让这一条过**：先缺的是名字；捕获是之后的、还没走到的第二件事（即便有了名字，被转置的被调方还得把捕获值收成自己的参数）。今天能过的是**按名字写出来的调用**（`g(x)`，见 `test/cases/compile/transforms/transpose_call.cml`），因为那种调用落成 FUNC 节点。纸面答案：无捕获那条欠 **2**（余切 2 时 **4**），带捕获那条欠 **2**（余切 3 时 **6**）。两条都登记成 `kind = "negative"` 的用例、拒绝行钉死——名字那一步落地时第一条会先变绿、第二条还会红，红在哪一眼就能看出区别。

**落地 2026-11-01（`lane-gs6`）**：名字那一步做了，而且**只有一个入口**——`GCGraph::staticFunctionGraphOf`
（`src/runtime/graph.cpp`，就在 `calleeIdOf` 旁边），连同它旁边的 `GCStaticFunctionAnswer`。它**拒绝**而不是猜：
节点必须是 DATA、必须有一个静态槽、类型必须是函数类型、槽里必须是函数值、该值必须指名一个图、而那个图必须是**本图记着的静态引用**
（`kGraphRefStatic`）；失败时回答里带原因，`reason` 只有七个词之一——
`not-a-node`、`not-a-data-node`、`no-static-slot`、`not-a-function-type`、`no-function-value`、
`function-names-no-graph`、`not-a-static-reference`，另加 `function-closure`（值是一个正在拼装的闭包：静态函数加捕获，运行时填成一个值，
它在这里不指图）。**静态槽的字节永远不会被当作 id 读**——这就是那个入口存在的理由，下一个想"直接读静态槽"的人先看这句。

量出来的两条：
- 无捕获（`double(x) = 2x`，`g(x) = let h = double; h(x)`）：**答了** `TRANSPOSE function=g graph=50 ... nodes=3`，`2.0`，余切 2 时 `4.0`——纸面答案 2；
- 带捕获（`g2(x) = let h = (y: float) => y*x; h(2.0)`）：仍拒，且**拒绝往后挪了一层**：
  `TRANSPOSE function=g2 skip=call node=4 callee=none value=3:0 with=2:4 reason=function-closure`。
  名字已经拿到（否则不会有 `reason`，A/B 关掉入口时这一行连 `reason` 都没有），现在缺的是**捕获**：被转置的被调方得把捕获值
  收成自己的参数。纸面答案仍是 2（余切 3 时 6）。

顺带一条同源的事实：转置后的调用必须把被调值放回原来的位置——原调用把被调方当 `with` 输入传，转置后的调用就得传一个**转置图的静态函数值**
（空闭包）；放错位置得到的是"CALL node 3 without callee input"。
