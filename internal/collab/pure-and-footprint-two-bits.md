# 一位拆两位：`pure`（承诺）与 footprint（足迹）

> 决策者 `D-1128-120`：这是 **CC 主线**，排在所有非 CC 活前面；CC 稿 `02-overview.tex:29` 讲的就是 view/edit 块，"块体读不到草稿"这件事不成立稿子就不成立。
> 本页是**三个车道共用的契约**：改之前先读这一页，改完对着"四类读者"一节核自己有没有读错位。

## 0 一句话

**`pure` 是一位、被四类读者用，而它们要两个不同的答案。** 所以拆成两位，**每一位只有一个出处**：

| 位 | 名字 | 回答什么 | 出处（唯一） |
|---|---|---|---|
| ① | **承诺** `pure` | "这个算子的子句**声明了**它不写任何资源、也不碰程序外" | `src/kernel/ops/include/camel/ops/operator_record.h:482` 的 `pure()` —— **按 `D-1128-111` 改**：由 `uses.stated() && uses.value.none()`（"没有任何访问"）改成 `uses.stated()` **且下面四条同时成立**：<br>(a) **没有一条 `mode != Read` 的访问**（不写）；<br>(b) **每一条访问的资源都是程序内的**：`ResourceClass::Handle` 的资源，或那两类只读资源 `graph`、`spelling`；<br>(c) **不分配**（`uses.value.allocates` 为假）；<br>(d) **不依赖程序外**（`uses.value.externalEffect` 为假、`uses.value.functionOperand` 为假 —— 跑一个函数值等于跑它没说的访问）。<br>**只看 (a) 是不够的**：读文件、读控制台**也是 `mode Read`**，"没有写"会把它们判成纯。`none()` 与"没有写"**不是一回事**：读只读资源的算子今天被 `none()` 判成不纯，这正是要拆掉的那一位；而"读到程序外"按 (b)**仍然不是纯** |
| ② | **足迹** `accessesWritableResource` | "这次调用访问的资源里，**有没有一个资源会被写**" | 新读法，两半各一个出处：<br>(a) **哪些资源**：算子自己声明的 accesses（`Record::uses` 的每一条 `ResourceAccess`）<br>(b) **那个资源会不会被写**：`mayBeWritten(Resource)` —— 由**所有**声明里 `mode != Read` 的 access 算出来的一个全局答案，**不是按类硬编码**（不许写死 `graph` / `spelling`） |

**关键区分（这是全部症结）**：`graph`、`spelling` 这类**只读资源永远不会被写** ⇒ 读它们的节点**仍然可摆**。读草稿（一个**可写**的 `Handle`）的节点不可摆，但**仍可丢弃、仍可重算**。`ResourceClass::Handle` 同时覆盖草稿与只读 handle，**所以"可写"不能从类上静态得出**，只能从"谁声明了写它"得出 —— 这就是 ②(b) 必须是一个全局扫描、而不是一句 `cls == Handle` 的原因。

## 0b 一处必须跟着动的旧检查（否则新形状自相矛盾）

`src/compile/src/gct/builder.cpp:1218` 今天是：

```cpp
if (clause->pure() && !clause->uses().empty())   // 声明写了 pure，却带了 accesses ⇒ 诊断 + throw
```

这条的前提是"纯 = 没有任何访问"。按新的 ①（纯 = **没有写**），"声明了 `pure` 且只读一个永远不被写的资源"是**合法且正是我们要的**，所以这条检查要改成**只拒绝"声明了不写、却声明了写"**（按新定义这类子句自相矛盾），而"读只读资源"**必须放行**。同一段的 `UsesPromiseViolated`（`:1260-1276`，查 var 参数）照旧保留。

改完 `pure` 的承诺由两处共同保证：**声明面**（本条检查）与**实现面**（`UsesPromiseViolated`）。**不许**为了让它绿把 `pure` 收回去。

## 1 四类读者，各读该读的那一位

| # | 读者 | 今天读什么 | 应当读 | 落点 |
|---|---|---|---|---|
| 1 | **声明语言的承诺**（规则库里"纯调用能否出现在条件里"） | `pure()` | **① 不变** | `src/compile/src/gir/builder.cpp:1214-1231`（`PureAndUses`，`:1218-1219`）；诊断 `builder_diagnostics.cpp:1799` / `:1884`，字串 `messages/semantic.cpp:1188` |
| 2 | **效应摘要 / `is_pure` 查询** | `pure()` | **① 不变** | `operator_record.h:479` 的 `effect()`、`:482`；查询侧 `src/kernel/graph/read.cpp:577-594` |
| 3 | **`Arrangeable` 可摆性** | `pure()`（当第五项用） | **② 新读法** | `src/kernel/facts/include/camel/facts/arrangeable.h:60-82`（第五项 `noAccessToWrittenResource`）＋ `src/kernel/facts/facts.cpp:1018-1055`（`terms.pure` 保持 `record->pure()`、**新项**填 ②）、判定 `facts.cpp:1333`；消费者 `builder_helpers.h:749-795`、`builder_resources.cpp:1343`/`:1376`、运行期 `graph.cpp:231` |
| 4 | **签名发布**（`pure` 的调用发布**空签名**，读根本不进节点） | `pure()` 当"能不能省掉签名" | **① 判"它不是 effectful"，但签名要照 footprint 里声明的 accesses 发** —— **不许再因为 `pure` 就发空签名把读丢掉** | `src/compile/src/gct/builder.cpp:1218-1231`（`pure` 发布空资源集那段）、`builder_resources.cpp` |

**第 4 类是本轮真正要动的地方**：它今天用"纯"当"没有资源可写"的代理，于是**纯算子读资源这件事在节点 footprint 里消失了**，顺序边因此建不起来。改成"按声明的 accesses 发布"，读才进节点，块体里的读才看得见。

## 2 判据（三格接力，今天不收口也要今天开足）

- **① 的定义按第 0 节改**（`D-1128-111` 的四条：不写、资源是程序内的、不分配、不依赖程序外），**改完之后不再重开**。
- **① 的反例判据（各一条，缺一条不算做完）**：
  - **`pure` 而声明了写一个资源 ⇒ 被拒**（`builder.cpp:1218` 那段收窄后的检查，报 `PureAndUses`）；
  - **`pure` 而读一个程序外的资源 ⇒ 被拒**（读文件 / 读控制台，`mode` 是 `Read` 也不算纯 —— 这是"只看 `mode != Read` 不够"的那条反例）。
- **② 的判据**：`mayBeWritten(Resource)` 与 `accessesWritableResource()` 各**只有一个出处**；`git grep` 证明没有第二处在推导同一件事（列清单：找到的每一处，要么收进、要么写明为什么不是同一件事）；块体三例（`edit d = f { … }` 里读得到值）转绿；**"pure 但读一个只读资源"的节点仍可摆**要一条读数；**"pure 但读一个可写资源"的节点不可摆**也要一条读数。
- **甲/乙/丙**：按 `feat/modules` 层基线（`fail 29 / known 1`）＋ **75 表 141 名**逐名判；**每条甲写一句"为什么被 pure 变意思打到"**；**旧法期望的改用例，真回归的修实现，不许为让它绿改语义**。
- `lane-editview-a` 手上那两笔（`63845ee572` 读声明 + `45f096735c` 体-序）**保留、但不宣称修好块体**。

**② 的两条读数（第五项接线后，在 `lane/arrangeable-two-bits@85101c529b` 上取；底 `origin/integration@97755d51e8`）**

**现场（承诺真、足迹假、仍可重算）**：`test/cases/loops/edges.cml` 的 node 13
```
FACT {"library":"facts","kind":"core.arrangeable",...,"anchors":[{"graph":"root","node":13}],"value":"reads a resource a write reaches",...}
```
理由串**只有**这一项 —— 若 ① 承诺为假，`toString` 会先打 `not pure`（逐个列出不成立者），所以这一行同时是"`pure` 仍为真"的读数；同一用例四个后端全绿（`loops.edges.nvm/.fvm/.jit/.prl` PASS）⇒ **"不可摆"不妨碍它被丢弃、被重算**。

**读只读资源仍可摆：语料里无实例；一条临时探针给出答案后已撤，不进仓库。** 探针读数（模型格，临时声明 `graph:probe_reads_graph` / `graph:probe_reads_draft`，跑完即撤）：
```
PROBE graph:probe_reads_graph pure=true  awr=false writes=false outside=false n=1 accesses=[ambient/graph read]
PROBE graph:probe_reads_draft pure=true  awr=true  writes=false outside=false n=1 accesses=[handle/graph::draft read]
```
⇒ **同一类资源的两个实例给出不同答案**（`awr=false` vs `awr=true`），答案来自**声明**（`mayBeWritten`）而不是资源类。

**以下是"语料里为什么没有实例"的现场（扫过一遍，成立）**：把 `test/cases` 全 4200 个 `.cml` 用 `camel inspect --fact core.arrangeable` 扫过，只得到上一条"仅第五项不成立"的节点；`camel inspect --ops` 显示**当前标准库被声明的可写资源只有 `graph::draft`（`class: handle`，`mode: rmw`：`graph::erase`/`add_constant`/`add_param`/`commit`）与 `profiler`（`class: ambient`）**，语料里**没有"读一个没人写的 `handle` / `graph` / `spelling`"的算子**（`src/stdlib/graph.cml:72 resource spelling readonly` 这类只读资源**有声明、没有算子读它**）。⇒ 第五项**目前只会因为"读草稿"而为假**；"读只读资源仍可摆"这条**等有人写出那样的算子**，取第一例的现成命令：
```
camel inspect --fact core.arrangeable <case>
```
（事实转储在 `inspect` 子命令下；`camel --fact …` 在 run 语法里是 unknown option。）**不为了凑读数造临时用例** —— 那是第二份真相。

**代价（消费者一侧，已另派一格）**：`test/cases/modules/tiles/resident_cpu_block.cml` 变红（`GpuScopeUnsupported: '#gpu' scope 'step'`），走 `src/stdlib/tiles/src/region_outline.cpp:155-167` 的 `keepsOrder` —— **"要保顺序"被当成了"留在 `#gpu` 作用域里"**，而这是两个问题。判定 **真回归、不是旧法期望**，**修在消费者**（`lane/gpu-scope-placement`），不在事实层。

## 3 不归这条线的一格

**编 stdlib 库时 prelude 的导出表整个为空（连 `map` 都取不到）** —— `rules` 那 5 条与 `schedulers` 编不过的根，**单开一格修**，不要并进这条主线。

## 4 台账

三版读数与四类读者的行号见 `internal/collab/pure-one-bit-four-readers.md`（含 `Compare-Object` 少 `-IncludeEqual` 时"丙恒为 0"那条教训）。

`2026-11-28` **第五项接线**（`lane/arrangeable-two-bits@85101c529b`，底 `origin/integration@97755d51e8`，两笔：`c8ae83150f` 模型格 cherry-pick + `85101c529b` 第五项）：`arrangeable.h` 五项齐全（`noAccessToWrittenResource`），`facts.cpp` 的 `terms.pure = record->pure()` **原样未动**、新增 `terms.noAccessToWrittenResource = !record->accessesWritableResource()`，`andTerms` 跟着加、`toString` 打 `reads a resource a write reaches`。`BUILD_EXIT=0`、`dirty 0`；capacity 18/18、compile 2734/2718/13、modules 2093/2057/**28**（基线 27，+1）。对 75 表 141 名：**甲 1 / 丙 40 / 乙 0 / 未跑 101**，覆盖 4845 条（`feat/compile` 114 计划 + `feat/modules` 85 计划 + `feat/mm/capacity`）。**甲那 1 条 = `resident_cpu_block`，真回归，修在消费者**（见 §2 末）。

`2026-11-28` **一条通用的读数规则（`D-1128-132`，不针对某一族）**：**一张红表只是同一选法的基线**。晋级表是"全快层"那个选法（含计划自己声明的风险抽样）；**点名计划文件**会把那份计划**整份跑**（`--tier` 不参与），`-Tier all` 与 `daily-all` 那张表是整份跑的选法 ⇒ **选法不同就不能相比**。**比红之前先确认两边选法相同**：同层同目标对晋级表，点名计划文件对 `daily-all` 表或同树基线。已写进 lane skill 的读数那节。apply 到现场：`mm.gc_poison_sweep` 在晋级表里 3 条红（抽样）、整份跑 42 条（`57838d1f3c`），两个数并存不矛盾；"多出来的 35/38 条"因此不是新红。

## 5. `pure()` 的读者全量清点（2026-11-28，`4d6bad8d80` 基准）

宽匹配 127 处、真 `.pure()` 调用 **30 处**。按"它问的是承诺还是足迹"分四类。

### 5.1 问"设备/作用域能不能跑"的那一族：7 处，**全部读承诺**（第五个读者）

| 位置 | 写法 | 后果 |
|---|---|---|
| `tiles/src/graph_regions.cpp:332-342` | `touchesNothing(oper) = record->pure() && !record->external()` | 一族的总闸 |
| `tiles/src/graph_regions.cpp:166` | `regionRoots` 里 `!touchesNothing` | 区域根随承诺变 |
| `tiles/src/graph_regions.cpp:371-372` | `deviceCapable = operations().count(oper) && touchesNothing` | 设备能力随承诺变 |
| `tiles/src/graph_regions.cpp:384` | `gpuRefusal` 里 `!touchesNothing ⇒ 忽略` | 拒绝理由随承诺变 |
| `tiles/src/census.cpp:61` | `operatorSurface`：`!touchesNothing ⇒ gap` | **`resident_cpu_block` 印的就是这个** |
| `tiles/src/region_outline.cpp:586` | host 边界判定 `!touchesNothing` | 段划分随承诺变 |
| `tiles/src/region_outline.cpp:155-174` | `keepsOrder`：`nodeIsFreelyArrangeable ⇒ 丢顺序边`（经 `facts.cpp:1333` 的 `all()`，pure 项 = `facts.cpp:1032`） | 顺序边随承诺变 |

病根：`facts.cpp:1032` `terms.pure = record->pure()` —— **承诺被"可摆性"直接当答案用**。

⇒ 只改 `touchesNothing` 一处不够：它上面 5 个调用点，下面还有 `census.cpp:61` 与 `region_outline.cpp:586` 两个直接消费者。**"改完还有任何一处由 `record->pure()` 决定设备/域"就是堵了一个洞、开着六个。**

### 5.2 混合：拿"有没有 accesses"核对"纯不纯"的 4 处（第三处出处的候选）

`pure` 一旦是"不写"，**声明了读的纯算子会被判矛盾或被拒**：

- `compile/src/gir/builder.cpp:1218` `pure && !uses.empty()` ⇒ `PureAndUses`
- `compile/src/gir/builder_modules.cpp:558` `written.pure() == (declared 空)`
- `kernel/core/operator_table.cpp:1022-1024` `traits->pure && !r.accesses.empty()` ⇒ "a pure operator touches no resource"
- `compile/src/gir/builder_when.cpp:2291` `!resources.pure()` ⇒ 拒绝折叠

### 5.3 问承诺但**决定发布**：改读足迹会变答案

`builder.cpp:1222`、`builder.cpp:1415`（`pure` ⇒ 发布空签名）。

### 5.4 纯承诺、改读足迹也不会变

`builder_diagnostics.cpp:242/284/1075/2147`、`introspect.cpp:284`、`resource_semantics.cpp:802`、`text.cpp:104/318/321`、`graph/read.cpp:587/595`、`pass.cpp:1131`、`cml.cpp:1429/1434`、`tiles/compiler.cpp:461`。

### 5.5 代码里已有的两处"不写"范本（第四条口径不该是第三套说法）

`sem::covers`（`resource_semantics.cpp:614-620`）已经在做"承诺 = 不写 ⇒ 只问 source 写不写"；`runtime::Effects::pure()`（`runtime/effects.h:40`）= `!external && !writesVariables`。**共享判定函数复用这两处的形式。**
