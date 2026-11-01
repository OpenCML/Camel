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
  （复核 2026-11-01：上面的行数是当天的文件量，今天读作 `split_operator.cpp` **196**、`jvp_operator.cpp` **263**、`transforms.cml` **116**、`vjp.cml` **94**，`split.cpp` **1474**、`jvp.cpp` **1305** —— 三个 `transforms/*.cpp` 随 `lane-gs6`、`lane-reinterp2` 两笔合并长出来，之后 `split.cpp` 又随 `914e09263`、`e0c206239`（残差元组自成一个类型）多了 96 行，`jvp.cpp` 随 `728b9f27c`、`4fbdc4e3a`（提升带上它来自的图）多了 27 行；`split.cpp` 之后又随 `fdb60ba49`（分支的切一刀：半边各自建 Brch/Join、每臂一个 region）多了 437 行；`vjp.cml` 随 `lane-m4-assembly` 的泛型化变短。命令：`node test/tools/check-doc-readings.mjs`。）

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