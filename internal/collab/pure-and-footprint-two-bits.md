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

## 3 不归这条线的一格

**编 stdlib 库时 prelude 的导出表整个为空（连 `map` 都取不到）** —— `rules` 那 5 条与 `schedulers` 编不过的根，**单开一格修**，不要并进这条主线。

## 4 台账

三版读数与四类读者的行号见 `internal/collab/pure-one-bit-four-readers.md`（含 `Compare-Object` 少 `-IncludeEqual` 时"丙恒为 0"那条教训）。