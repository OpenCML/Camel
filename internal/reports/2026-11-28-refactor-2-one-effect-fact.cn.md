# 重构 2「效应事实只有一份」第一笔：要删的清单 / 替换后的形状 / 比对语料

> **状态（2026-11-28 18:1x）：本页定稿**，不含 `src/` 改动。三样交付齐（§2 删的清单、§3 替换的形状、§4 比对语料），第 1 刀的写死范围与四条判据在 §7；开工前提未满足（`lane/c4-round-source` 未进 integration，枝尖 `ae14f65247` / 改动 `63384055bc`，正在换底重做）。§8 记的是 `lane/two-bits-model` 的参考原文（该枝只作材料、已删）。

- 依据：`internal/plans/2026-11-28-refactor-not-patch.cn.md`（`fsh-dev@68fcfc6a57`）第 2 件。
- 底：枝 `lane/draft-read-cut` 建在 `origin/integration@4f59be8265d2b69c900ee3b6ee7189d4441f423a`（建枝时的尖端；integration 现已推进到 `c6c2afe53f`，第一刀开工前 rebase 再取一次行号）。本页首先生于 `integration@9e8d7303ad`，后随枝重挂；**`src/` 全文未动**，行号相对 integration 仍成立。
- 行号在这一底上逐条核对过（`mayBeWritten` `operator_table.cpp:146`、`accessesWritableResource` `:1275-1290`、第五项 `facts.cpp:1332`、`Arrangeable` 的五个字段 `arrangeable.h:62/74/78` 都在同一位置）；在 `lane/arrangeable-two-bits@6b5c132e8f` 上同形。`src/` 一动就要重取。
- 材料：`internal/collab/pure-and-footprint-two-bits.md`（一位拆两位的契约，含 `pure()` 读者全量清点）、`lane/c4-round-source`、§6 记的两笔被取代的提交（38 个查询纯度那一版，**不进合并队**，转为本件的材料）。
- **sha 写法（全页统一）**：写 `c4-round-source@63384055bc` 指**改动那一笔**（自检改成"纯 ⇒ 不许写"，`operator_table.cpp:1012` 的 `a pure operator writes a resource` 在它里面）；**枝尖 = `ae14f65247`**，是它的子提交，只多一页 `internal/collab/g3-block-callback-erase.zh.md`（`+36 −5`），**`src/` 零差异**。合并位按枝尖取，改动的引用一律用 `63384055bc`。

## 0 一句话

**同一个问题今天有六份答案，各有各的写法，于是它们会漂。** 目标是：`.cml` 一份声明 → 一份程序值事实（每个节点的足迹 + 这个值里的写集）→ 纯 / 可摆 / 能否上设备 / 遍能否先算 / 能否进规则条件，各有**一处**判定函数。

"会漂"不是推测，本枝量到过两次：

1. **第五项问的是整张算子表**：第五项这一位（`noAccessToWrittenResource`，`facts.cpp:1332` = `!record->accessesWritableResource()`）问的是"这条读访问的资源，有没有**任何声明**写它" —— `mayBeWritten`（`src/kernel/core/operator_table.cpp:146`）扫的是全部声明，而 `state:set` 一注册，`Ref` 就有写者。所以一个**程序里一次 `set` 都没有**的 `state::get`，它的第五项**位**仍是假（`/tmp/refprobe/pc_write_away.cml`，读数与"为什么它今天分不出答案"见 §4.4：**整条 `all()` 早被第四项定死，不是第五项漏答**）。
2. **"纯"有两份**：`ops::Record::pure()`（`src/kernel/ops/include/camel/ops/operator_record.h:487`）说"子句没声明写"，`sem::ResourceSignature::pure()`（`src/kernel/include/camel/core/resource_semantics.h:378`）说"签名上没有访问"，`OperatorTraits::pure`（`operator_table.cpp:96-115` 推、`operator_table.h:253` 存）说"效应等于 Pure" —— 三处都在回答"纯不纯"，条件不同。38 个查询那一笔就是撞在这上面：声明了 `Read` 之后，记录说 `pure`（`clause_pure: true`），显示说 `state`（`effect()` 由访问列表现推），而编译器的自检说"纯算子不许碰资源"并拒绝装载。

## 1 今天有几份答案（都在这棵树上，file:line 是底提交的）

| # | 答案 | 出处 | 行数 | 谁**写**它 | 谁**读**它 |
|---|---|---|---|---|---|
| 1 | 声明词表：`{name, pure}` 与 `declareResources` 两份 | `operator_table.cpp:308-321`（`declare`）、`:242-306`（`set`，把词写成 `declaredEffect` 的那段 `:250-269`） | 14 + 65 | 每个 stdlib 模块的 `load()` | 自己（写进记录） |
| 2 | `sem::Effect effect()`（由访问列表现推） | `src/kernel/core/operator_semantics.cpp:299-311`；`effectName` `:212-227` | 13 + 16 | —（推的） | `traitsOf`、`--ops` 的 `purity` 列、`cml.cpp:1334`、`mayBeRecomputed` |
| 3 | `OperatorTraits::pure` / `stateOnly` | `operator_table.h:253-…`；推在 `operator_table.cpp:96-115` | 20（映射） | 无（推的） | 自检 `:1022-1033`、`writesForTheRegion` `:469-472`、`cml.cpp`、tiles 一族 |
| 4 | `ops::Record::pure()` | `operator_record.h:487-492` | 6 | 无（由 `uses` 推的） | 9 处：`gct/builder.cpp:1214/1218/1222/1415`、`builder_diagnostics.cpp:242/284`、`builder_resources.cpp:507`、`facts.cpp:1324`、`introspect.cpp:284`（印 `clause_pure`） |
| 5 | `ops::Record::accessesWritableResource()` | `operator_table.cpp:1275-1290`（16 行）＋ `Record::declaresWrite` `:1263-1273`（11 行） | 27 | 无（推的） | `facts.cpp:1332`（第五项） |
| 6 | `OperatorTable::mayBeWritten(Resource)` —— **扫整张算子表** | `operator_table.cpp:146-162`；声明 `operator_table.h:463` | 17 | — | `accessesWritableResource` 一处；探针/读数走它 |
| 7 | `sem::ResourceSignature::pure()` | `resource_semantics.h:378-382` | 5 | `gct/builder.cpp:1190-1217`（`resourceSignatureOf` 发签名） | 13 处：`builder_diagnostics.cpp:200/1075`、`builder_modules.cpp:421/558`、`builder_when.cpp:2291`、`resource_semantics.cpp:618/802`、`type/text.cpp:104/318/321`、`test/commit_checks.cpp:298`、`cml.cpp:1429/1434`、`tiles/src/compiler.cpp:461` |
| 8 | `runtime::EffectSummary::pure()` | `src/kernel/include/camel/runtime/effects.h:40` | 1 | `effect_walk`（唯一一处算摘要） | `graph/read.cpp:591/599`（`is_pure` 查询）、`pass.cpp:1052`、`test/block_body.cpp:416` |
| 9 | `facts::Arrangeable.pure` | `src/kernel/facts/include/camel/facts/arrangeable.h:62`；填在 `facts.cpp:1324` | 1 + 1 | `termsOfOperator`（`facts.cpp:1310-1353`） | `Arrangeable::all()`（`arrangeable.h:77-79`）及全部可摆性读者 |
| 10 | 各遍自带的纯度判断 | 见 §2.4 | 9 处 | — | 各自 |

**病根一句话**：3、4、7、9 是同一句话的四种写法；5、6 是"这个资源会被写"的两种问法（一种扫表、一种问记录）；10 是各遍自认为可以自己判。

## 2 要删的清单

### 2.1 纯度表（与 `pure` 并列的那份声明）

| 删 | 位置 | 行数 | 为什么它是第二份 |
|---|---|---|---|
| `OperatorTable::declare(protocol, {{name, bool pure}})` | `operator_table.cpp:308-321` | 14 | 与 `declareResources` 声明同一件事的两种写法；38 个查询那一笔必须写两次才能同时说"纯"和"读草稿" |
| `OperatorTable::set` 里把"词"写成 `declaredEffect` 的段 | `operator_table.cpp:250-269` | 20 | 词的语义 = 访问列表现推（`effect()`），是同一句话的两种存法 |
| `OperatorTraits::pure` / `::stateOnly` 及其映射 | `operator_table.h:253-…`、`operator_table.cpp:96-115` | 20 + 结构体 | 由 `effect()` 再推一次；读者应直接问 `effect()`（或问事实） |
| `stateUsesInRecord` 的拷贝路径 | `operator_table.cpp:194-213` | 20 | 访问列表在 `OperatorSemantics::resources` 与 `ops::Record::uses.value` 各存一份；只留记录那份 |

### 2.2 两个 `pure()`

| 删 | 位置 | 行数 | 谁写 / 谁读（现状） | 处置 |
|---|---|---|---|---|
| `ops::Record::pure()` | `operator_record.h:487-492` | 6 | 无人写（由 `uses` 推）；9 处读（§1 行 4） | 保留**一个** `declaresNoWrite()` 定义在同一处，`ResourceSignature::pure()` 改为调它 / 或让签名侧只问"没有写"的同一条判定 |
| `sem::ResourceSignature::pure()` | `resource_semantics.h:378-382` | 5 | 由 `resourceSignatureOf`（`gct/builder.cpp:1190-1217`）发；13 处读（§1 行 7） | 同上：定义合一处，两处读者各自只读自己那份数据的"没有写" |

> 两者不是同一个对象（一个说算子子句，一个说函数签名），**但"没写"必须是同一句判定**：今天一个用 `uses.stated() && !external && !functionOperand && !allocates && !declaresWrite()`，另一个用 `mode==Concrete && 全空 && !allocates && !external`。合成一处之后，`pure_touches` 那条期望的措辞由那句判定给出。

### 2.3 `mayBeWritten` 的整表扫描

| 删 | 位置 | 行数 |
|---|---|---|
| `OperatorTable::mayBeWritten(const sem::Resource &)`（`for (OperId id = 1; id < count; ++id)` 扫全部记录） | `operator_table.cpp:146-162`，声明 `operator_table.h:463` | 17 |
| `Record::accessesWritableResource()`（对每条读访问调上者） | `operator_table.cpp:1275-1290` | 16 |
| `Record::declaresWrite(of)` 保留（它是"这一条访问写不写"的唯一判定），但不再被"整表扫"使用 | `operator_table.cpp:1263-1273` | 11（保留） |

**替换**：问"这个键在**这个程序值**里有没有写者"，不再问整张表。见 §3.2。

### 2.4 各遍自带的纯度判断（`git grep -n "isPure\|pureOper\|pure()" src/stdlib src/compile` 全量，逐个一行）

| 位置 | 今天写的 | 它其实在问 | 换成 |
|---|---|---|---|
| `src/stdlib/std/cxx/opt/generic/rewrite.cpp:83` | `isPureOper(transaction,id)` → `OperatorTable::mayBeRecomputed(body->oper)` | "遍能否先替它算出来" | 判定函数 ④（`mayBeRecomputed`），不改读者，只改它的实现（读事实而非 `declaredEffect`） |
| `src/stdlib/std/cxx/opt/generic/screen.cpp:81` | `isPureOperNode` → 同上（另加节点形状） | 同上 | 同上（`screen.cpp:142/197/274` 三个调用点跟着） |
| `src/stdlib/std/cxx/opt/generic/algebra.cpp:236` | `!isPureOper(transaction,id)` | 同上 | 同上 |
| `src/stdlib/std/cxx/opt/generic/generic.cpp:1425/1509/1682/1915` | `isPureOper(...)` | 同上 | 同上（`1682` 另加 `isWrittenInPlace`/`readsVariable`，那两条各自有出处，不动） |
| `src/stdlib/std/cml/cml.cpp:1334-1337` | `isPureOper` → `effectOf(oper) == Effect::Pure` | 承诺 | 判定函数 ①（`pure`） |
| `src/stdlib/std/cml/cml.cpp:1298/1408` | 调用上条 | 同上 | 同上 |
| `src/stdlib/std/graph/read.cpp:579-603`（`src/stdlib/graph/read.cpp`） | `isPureKernel`（`is_pure` 查询）：冻结图读 `core.operatorEffects`，草稿读 `declaredEffectsOfOperator` | 承诺 | 判定函数 ①，两条入口各读同一份声明的同一句 |
| `src/stdlib/std/tiles/src/graph_regions.cpp:332-342` | `touchesNothing = record->pure() && !record->external()` | **足迹**（设备能不能跑） | 判定函数 ③；该处已在 `lane/gpu-scope-placement@9c5fce1323` 改成 `!external && uses.value.none()`，其 6 个消费者（`graph_regions.cpp:166/371-372/384`、`census.cpp:61`、`region_outline.cpp:586`）随它走 |
| `src/compile/src/gir/builder_diagnostics.cpp:200/242/284/1075` | `func->resources().pure()` / `clause->pure()` | 承诺（诊断） | 判定函数 ①（合一处后的那句） |
| `src/compile/src/gir/builder_modules.cpp:421/558` | `signature.pure()` / `written.pure() == declared 空` | 承诺 | ① + `writesSomething`（见 `c4-round-source@63384055bc` 的形状） |
| `src/compile/src/gir/builder_resources.cpp:507` | `clause->pure()` | 承诺 | ① |
| `src/compile/src/gir/builder_when.cpp:2291` | `!resources.pure()` ⇒ 拒绝折叠 | 承诺 | ① |
| `src/compile/src/gct/builder.cpp:1214-1231`（`PureAndUses`）、`:1415` | `clause->pure() && !uses.empty()` ⇒ 诊断；`pure ⇒ 发空签名` | 承诺 | 已经由 `c4-round-source@63384055bc` 收窄成"纯 ⇒ 不许写"，签名按声明的访问发 |

`kernel/execute/pass.cpp:1052`、`test/block_body.cpp:416`、`test/commit_checks.cpp:298`、`type/text.cpp:104/318/321`、`tiles/compiler.cpp:461` 是 `EffectSummary::pure()` / `ResourceSignature::pure()` 的消费者，**不是独立判断**：它们跟着 ①③ 的定义走，不改逻辑。

## 3 替换后的形状

### 3.1 一份声明

- **唯一的写入口**：`.cml` 的 `pure` / `uses` / `panic` / 分配（标准库与内置算子都走 `table.declareResources`，`operator_table.cpp` 里把它写成 `ops::Record` 的 `uses` 子句与 `panic` 事实）。C++ 一侧**不再有任何**"另立纯度"的入口：`declare(protocol, {{name, bool}})` 删除，`OperatorTraits::pure/stateOnly` 删除，`OperatorSemantics::declaredEffect` 删除（`effect()` 只从 `resources` 推，或直接由记录回答）。
- **一份存储**：访问列表只存 `ops::Record::uses.value.accesses`（`stateUsesInRecord` 的拷贝路径删除，`OperatorSemantics::resources` 不再另存）。
- **落地文件**：`src/kernel/ops/include/camel/ops/operator_record.h`（记录的声明面）、`src/kernel/core/operator_table.cpp`（只保留"把 `.cml` 的话写进记录"这一段，`declareResources` 的路径）、`src/kernel/core/operator_semantics.cpp`（`effect()` 一处推导）。

### 3.2 一份程序值事实

| 事实 | 今天 | 目标 | 落点 |
|---|---|---|---|
| 每个节点的足迹 | 有：`core.nodeEffects` / `core.operatorEffects`，一处算（`effect_walk.h`，`facts.cpp` 的 walk） | 不变 | `src/kernel/facts/`（`effect_walk.h` 私有、`facts.cpp` 对外） |
| **这个值里的写集** | **没有**。第五项问的是整张算子表 | 新增：一个值（一张图）里所有写访问的键集合，随值算一次 | `facts.cpp` 的 `termsOfOperator` 旁（`facts.cpp:1310-1353` 那段之上），键 = `sem::Resource`（`{cls, name}`，`resource_semantics.h` 的 `struct Resource`） |
| 第五项 | `!record->accessesWritableResource()` → 整表扫 | `readKeyIsWrittenInThisValue(valueWrites, access.resource)` —— 纯函数，一处定义 | `facts.cpp`，紧邻 `Arrangeable` 的填写处（`facts.cpp:1332`） |

**写集怎么取：上游闭包、四类边。** 一个节点"读的键有没有写者"这个问题，问的不是整张算子表，是**这个节点在它所在的值里的上游闭包**：沿 `src/kernel/graph/graph_view.h:167-180` 的四类边 —— `Data`（值被消费）、`Sync`（同一 `sync` 块里写在它前面）、`Order`（两者都跑时排先后，不携带"used"）、`Wait`（`sync` 半边与第三组输入里的控制输入，合并成一种）——向上游走到闭包，把闭包里每个写访问的键收进写集，再问"这个键在不在里面"。闭包是**有向的、按值的**：同一条 `state:set` 在上游 ⇒ 下游的 `get` 不可摆；不在上游（程序里根本没有写，或写在下游）⇒ 可摆。§4.4 两个探针正是这条的两个方向。

**键从哪来**：编译期已经有 `Builder::BodyReach`（`src/compile/src/gir/builder.h:2901`）与 `handleKeyOf`（`:3005`）走一个函数体、把有名字的句柄收成键（`builder_resources.cpp:144` 起）。这与 `D-1128-145` 的"按草稿取键"是**同一套机制**：`BodyReach` 产出键，事实层收键成"这个值的写集"，判定函数只读这份集合。**分工**：

- `BodyReach` / `handleKeyOf`：**编译期、按值**，产出"这个函数体读/写到了哪些键"；
- `facts.cpp` 的 `GraphView` 一侧（`GraphView` 入口 `facts.cpp:1373` 的 `termsOfOperNode` 与 `ArrangeWalk::at`，`:1420`）：**事实层、按节点**，把"这个节点访问的键"与"这个值的写集"比一次，填第五项；
- `Arrangeable::all()`（`arrangeable.h:77-79`）：唯一的合取处，别处不再 AND 五个项。

> 形状要点：**写集是"值"的属性，不是"算子表"的属性**。`state:set` 在表里存在，不代表某个程序的某个值里有写；这是 `pc_write_away` 那条缺陷的根，也是"没有 `set` 的程序里 `get` 就该可摆"的实现位置。

### 3.3 五个判定函数，各一处

| 判定 | 唯一出处 | 读者 |
|---|---|---|
| ① 纯（承诺） | `ops::Record::declaresNoWrite()`（由 `operator_record.h:487` 的 `pure()` 收敛而来，与 `ResourceSignature` 共用**同一句**） | 诊断、签名发布、`Arrangeable.pure`、规则条件 |
| ② 可摆 | `Arrangeable` 五项 + `Arrangeable::all()` + `nodeIsFreelyArrangeable`（`arrangeable.h:77-79` 起，`facts.cpp:1535` 的 `nodeArrangeable`） | 改写、放置、`keepsOrder` |
| ③ 能否在设备上跑 | `tiles::touchesNothing`（读足迹，`!external && uses.value.none()`；`lane/gpu-scope-placement@9c5fce1323` 已改） | tiles 六处消费者 |
| ④ 遍能否先替它算出来 | `OperatorTable::mayBeRecomputed`（`operator_table.cpp:364-382`） | `rewrite.cpp:83`、`screen.cpp:81`、`algebra/generic` 五处、`graph.cml:182` 的 `may_be_recomputed` |
| ⑤ 能否进规则条件 | `PureAndUses` 的收窄检查（`gct/builder.cpp:1218`），判据 = `sem::writesSomething(sem::signatureOf(r, 0))`（`lane/c4-round-source@63384055bc` 已在用的形状） | 规则库、`builder_when.cpp:2291` |

## 4 比对语料（"换了之后答案不变"的证据从哪来）

### 4.1 38 个查询那批

- 声明面：`camel inspect --ops test/cases/modules/stdlib/registry_all.cml` —— 66 个 `graph` 算子，其中 **38 个读草稿的查询**（`graphs nodes params exit inputs with_inputs sync_inputs order_inputs users value_users is_constant constant_bytes kind op is_pure relative_cost may_be_recomputed may_fault may_be_rearranged type_of callee tags declaration_tags tag_graphs rule_of is_anchor is_written_slot origin show region_of region_kind region_parent id constant effects shape_text permissions callee_self`）、11 个改草稿的编辑（`ReadModifyWrite`）、8 个句柄定死的（`eq`/`ne`/`creation_id`/`op_name`/`type_name`/`tag_name`/`name`/三个吃函数值的）。
- 期望的答案：这 38 个在"纯=不写"下**仍是 `pure`**，且带着一条 `Read`；`inspect --ops` 的 `purity` 列由 `effect()` 推，读草稿的会显示 `state`（这说明"显示"与"承诺"是两件事，正是本件要合掉的那条缝）。
- 计划：17 个 `graph*` 计划逐名（`graph_read 1/1`、`graph_space 5/5`、`graph_edits 9/9`、`graph_entry_graph 2/2`、`graph_facts 11/11`、`graph_function 3/3`、`graph_inline 3/3`、`graph_outline 0/2`、`graph_pass_function 3/3`、`graph_values 47/47`、`graph 78/80`、`graph_faces 5/5`、`graph_identity 2/2`、`graph_draft_ledger 2/2`、`program_graphs 1/1`、`same_graph 1/1`），在 `6b5c132e8f` 上取。

### 4.2 `resources.plan.toml`

`test/plans/feat/modules/resources.plan.toml`（用例 `test/cases/resources/effects.cml`，`test::resources`）：一条被拒声明一条规则，正是"声明面"的全量反例集。**`pure_touches` 那句期望文案**是这支温度计：`lane/c4-round-source@63384055bc` 已把它从 `a pure operator touches no resource` 改成 `a pure operator writes a resource`（对应"纯 ⇒ 不许写"）。本件落地后，这句文案由 §3.3 ① 的那句判定给出，**只改一次**。

### 4.3 `arrangeable_families`

`test/plans/feat/tools/arrangeable_families.plan.toml`：在 `6b5c132e8f` 上 **7/7**（换底前是 5/7，两条红是 `facts.cpp:1483/1493` 的聚合初始化缺陷）。第五项改成"按值"之后，这 7 条必须逐名仍绿；任何一条变红都要说清是"答案变了"还是"实现变了"。

### 4.4 两个新探针：**今天分不出答案，原因在第四项，不在第五项**

| 探针 | 内容 | 今天 | 第一批实现之后必须 |
|---|---|---|---|
| `/tmp/refprobe/pc_write_away.cml` | 循环里只 `state::get`，**程序里没有 `set`** | `core.arrangeable` = `reads a resource a write reaches`，但这是**每图一条**的聚合（见下） | **可摆**（第五项为真） |
| `/tmp/refprobe/pc_write_before.cml` | 同类循环，循环前有一处 `state::set` | 同上：两条**分不出** | **不可摆**（第五项为假） |

**分不出的原因（`lane-editview-a` 的 A/B 读数，Lead 已核其推导）**：不是第五项被跳过，而是**第四项先定了答案**。

- 算子侧：`:var/get` 的两边都是 `free=0 kept=1`，而 `camel inspect --ops` 的原文是 `":var/get","purity":"state",…,"clause_pure":true,…,"may_fault":true` ⇒ **"不能故障"先答 no，第五项没轮到**。
- **判据今天本身不漏**：`facts.cpp:1332` 的第五项就是 `!record->accessesWritableResource()`；`operator_table.cpp:1285` 只把**读**算进来（`374c15fde7` 改的正是这一句）。
- 真"漏掉写"在 `16:58` 之前：`operator_record.h:482` 的旧 `pure()` 问的是"有没有**一次访问**"而不是"有没有**写**"，这一笔**已被 `fc66a33e09` 修掉**。
- **全表只有 4 个算子声明了读**（`:var/after`、`:var/get`、`:var/peek`、`:op/mem_blocks`），**四个都 `may_fault=true`** ⇒ 今天**不存在**"纯、不故障、又读某资源"的算子。

**⇒ 语料的前提（必须写在前面）**：要验第五项，**先在表上造出一个"纯、不故障、且读某资源"的算子** —— 这恰好就是本件（"效应事实只有一份"）要交付的东西（§2.1/§2.2 把两份声明合一份，`pure` 与 `Read` 不再互斥）。所以这条探针**排在第 1 刀（两个 `pure()` 合一）之后**，不是之前；今天拿它当判据会得出错结论。

**另一条现在就能用的观测面**：图层的 `core.arrangeable`（§4.6 那四个程序走的就是这一层）—— 它按图聚合、不因某个算子的 `may_fault` 而失去读数能力，现在就能量。**但它对这两个探针今天也给出相同的事实**（我量的原文：两个程序的 `main` 节点都是 `not pure, external effect, may fault, may not terminate, reads a resource a write reaches`），因为这条事实是**每图一条**、且 `external effect` / `may fault` 早把它定死。⇒ 图层能用来量"没有位移"（§4.6 那组），**不能**用来量"只有 `get` / 再加 `set` 的差别"；那个差别要等写集按值算出来（第 2 刀）才可测。

### 4.5 另外几条现成的读数

- `probe_a` / `probe_b`（两处相邻 `get`、`get`→`set` 各一）：四个配置 stdout 全对（`0/0`、`0`），GIR 里 `N02 -> N03 [ORDER]`、`N02 -> N04 [ORDER]` 都在 —— 这是"读是依赖、写是效果"的现场，改判定函数时**必须仍是这样**。
- `test/cases/loops/edges.cml` node 13：`core.arrangeable` 只有第五项不成立（`reads a resource a write reaches`），四个后端全绿 —— "不可摆"不妨碍被丢弃、被重算的代表。
- `examples/case-study` 的 `drop_print`：本枝尖端**改前就红**（`graph.erase` 拒绝锚点，`edits.cml:90:9`，rc=1，stdout 空）—— 它不是本件的判据，但比对的语料里必须带着它，免得把一条旧红算进来。

### 4.6 旧底的对照读数（**旧底、仅参考**，不冒充新底读数）

> 这一组量在 `lane/arrangeable-two-bits@562888a151` 那一代底上（"38 个查询纯度"那两笔在改前/改后各跑一次），**底不同就不能与新底相比**；留着它是"没有位移"的证据 —— 声明前后，程序侧的事实**逐字相同**。四程序 `core.arrangeable` 的 `diff` 全空（`drop_print` 的 61 行逐字相同）：

| 程序 | 改前 → 改后 |
|---|---|
| `/tmp/refprobe/probe_a.cml` | `core.arrangeable` 逐字相同（改前/改后 `diff` 空） |
| `/tmp/refprobe/probe_b.cml` | 同上 |
| `/tmp/refprobe/pc_write_before.cml` | 同上 |
| `examples/case-study` 的 `drop_print`（61 行事实） | 同上；`rc=1`、stdout 0 字节、`graph.erase` 拒绝锚点，改前改后同一条 |

未改动的还有：`probe_a`/`probe_b` 四配置 stdout（`0 0`、`0`）与三种 `std::stats`（`probe_a` 9/12/5 → 14/18/10 → 9/12/5；`probe_b` 9/11/4 → 14/16/8 → 9/11/4）。**新底上的同组读数等 `c4-round-source` 落地后再量**（那时不需要借自检）。

## 5 顺序与判据（本件落地时的自检）

1. **先去 3、4 的重复**（§2.1、§2.2）：一处定义"没写"，其余读者改成调它。判据：`resources.plan.toml` 逐条仍按同一句期望；`inspect --ops` 的 `clause_pure` 与 `purity` 两列不再互相矛盾地出现在同一个算子上（38 个查询是现成的例子）。
2. **再把第五项从"扫表"改成"按值"**（§2.3、§3.2）：判据 = §4.4 那两个探针**分开** —— 但**这一步之前必须先有"纯、不故障、且读某资源"的算子**（今天全表 4 个读资源的算子都 `may_fault=true`，第四项先定，§4.4），所以第 2 刀的判据链是：第 1 刀把两份 `pure()` 合一处 → 造出那样的算子 → 探针才分得开；同时 §4.3 的 7 条、§4.1 的 17 个计划逐名不变。
3. **最后收各遍的判断**（§2.4）：`git grep` 清单里每一处要么收进 §3.3 的五个判定，要么写明为什么它不是同一件事（例如 `isWrittenInPlace`、`readsVariable`）。
4. 全程：`BUILD_EXIT=0`、`dirty 0`、不 push；读数在宿主 `88zjwei` 上取，标题带 sha。

## 6 被取代的两笔（不进合并队）

这两笔是本件开工前那条补丁车道（"38 个查询纯度 / 纯拆两位 / 第五项 (a)"）的读数材料，**决策者 `D-1128-176` 五与 `D-1128-180` ② 已用本件取代它们**。枝 `lane/draft-read-decl` 已 `reset --hard origin/integration@9e8d7303ad` 重新起（它们只留在这张表里；sha 在仓库对象里仍可取 `git show <sha>`，**不随枝进入任何合并**）：

| sha | 内容 | 为什么被取代 |
|---|---|---|
| `a46c7b90b4` | `graph: the read side declares the draft it reads…`：38 个只读查询声明 `Read(graph::draft)`，并把它们的 `pure` 改成 `state`（`src/stdlib/graph/module.cpp`，`+112 −0`） | 它是在**旧的"两份声明"**上挪：为了让"纯算子不许碰资源"的自检放行，只能把纯度改成 `state` —— 而这正是本件要合掉的那条缝（承诺与显示各一份）。本件落地后，同一个算子只写一份声明，`pure` 与 `Read` 不再互斥，不需要挪纯度。 |
| `0a34c6fa49` | `graph: the read side keeps its purity and still declares the draft it reads`：纯度改回 `pure`、`Read` 保留（`+5 −50`），装载被旧自检拒绝，`graph_edits` 0/9 | 同一件事的反方向挪法：它证明"旧规则 + 新声明"正面冲突（`operator_table.cpp:1013-1035` 的 `resources: a pure operator touches no resource`），而这条规则已经在 `lane/c4-round-source@63384055bc` 改成"纯 ⇒ 不许写"。真正的解不是改哪一边的词，而是让 C++ 不再另立纯度表（§2.1、§3.1）。 |

读数留档（两笔各自量到的，均在宿主 `88zjwei` 上取）：
- `a46c7b90b4` 的版本能装载、能跑：`inspect --ops` 显示 38 个 `purity "state"` + `{"class":"handle","name":"graph::draft","binding":"norm 0","mode":"read"}`；17 个 `graph*` 计划与 `arrangeable_families` 的读数**与改前逐名相同**（`arrangeable_families` 5/7，红为 `loops_and_branches`、`branch_and_effect`；`graph_outline` 0/2；`graph` 78/80）。
- `0a34c6fa49` 的版本装载被拒（`resources: a pure operator touches no resource`），借 `lane/c4-round-source@63384055bc` 的自检后恢复：`graph_edits` 9/9、`arrangeable_families` **7/7**（底 `6b5c132e8f`）、其余 16 个 `graph*` 计划逐名不变（`graph_outline` 0/2、`graph` 78/80 两条旧红仍在）。
- **两笔都没能证明"读草稿的节点该不该可摆"**：因为第五项问的是整张算子表（§0 第 1 条），程序里有没有写不影响答案 —— 这个答案只能由本件（按值取写集）给出。

## 7 第 1 刀的写死范围与判据（待 `c4-round-source` 落地）

**范围（只做这一件）**：把 `Record::pure()`（`operator_record.h:487-492`）与 `sem::ResourceSignature::pure()`（`resource_semantics.h:378-382`）**改成调用同一个 `declaresNoWrite(...)`**，实现**只写一处**。**不动** 9 + 13 处读者的语义（读者列表见 §1 行 4 与行 7），**不动** `mayBeWritten` 整表扫描（§2.3），**不动**第五项（§3.2，那是第 2 刀）。

**判据（四条，缺一条不算做完）**：

1. **81 表逐名新增红 0**（同一选法相比，选法不同不比 —— 见 `pure-and-footprint-two-bits.md` §4 那条读数规则）。
2. `camel inspect --ops test/cases/modules/stdlib/registry_all.cml` 的 **38 个查询的 `pure`/`read` 列与改前逐字相同**（贴 `diff` 空）。
3. `test/plans/feat/modules/resources.plan.toml` 的 `pure_touches` 文案按 `lane/c4-round-source` 那句：`a pure operator writes a resource` —— **改动在 `63384055bc`**（`operator_table.cpp:1012`），**枝尖 `ae14f65247`** 只多一页 collab 文档、`src/` 零差异。
4. `BUILD_EXIT=0`、`dirty 0`、`git diff --numstat` 贴出、不 push；读数在宿主 `88zjwei` 上取，标题带 sha。

**开工前提（现在未满足，故未开工）**：`origin/integration` 尚未包含 `lane/c4-round-source` —— **枝尖 `ae14f65247`**，改动 `63384055bc`。2026-11-28 18:1x 核对：integration = `c6c2afe53f`，`merge-base --is-ancestor ae14f65247 origin/integration` 退出码 1（只有 `lane/c4-round-source` 带着它）；那条枝**正在换底重做**（合并位停手交上来的冲突：`handleAccessKey` 与 `draftKeyOf` 两半合到一处），落点已定。**换底落地并合进 integration 之后**本枝 rebase 到新尖端，第一条按本节范围动手。

**同批的边界（已核，不撞）**：`lane-enter-shape` 的 `lane/jgir-accesses`（`git diff --stat origin/integration...lane/jgir-accesses`）只动 `src/stdlib/std/gir/dot/graphviz.cpp`、`src/stdlib/std/gir/json/gir.cpp`、`docs/technical/gir-interchange.md`、`docs/technical/61_capture_census.md`、`scripts/check-capture-census.mjs` 与 `test/cases/trans/*`、几个 `.jgir` 黄金 —— **没有动 `src/kernel/facts/` 的足迹接口，也没有动 `operator_record.h` / `resource_semantics.h`**。它管 jgir 那一份事实，本件管判定函数；第 2 刀（第五项按值）之前再核一次。

## 8 `lane/two-bits-model` 的参考原文（该枝只作材料、不进合并；读到的抄在这里，枝已删）

决策者裁定：该枝的两笔（`Record::accessesOutsideProgram()` 与 `ResourceSignature::pure()`）**正是本件要删的那两份真相之一**，所以它是材料、不是要合进来的东西。下面把它记的两处 `pure()` 与"承诺怎么拼"的原文抄下来，供第 1 刀（§7）直接用。

### 8.1 今天两处 `pure()` 各在哪（本页底，`src/` 未动）

| 位置 | 原文 | 它问的问题 |
|---|---|---|
| `src/kernel/ops/include/camel/ops/operator_record.h:487-492` | `bool pure() const { return uses.stated() && !uses.value.externalEffect && !uses.value.functionOperand && !uses.value.allocates && !declaresWrite(); }` | 子句**声明了没写**：已声明、无外部效果、无函数操作数、不分配、没有一条写访问。**读不影响它**。 |
| `src/kernel/include/camel/core/resource_semantics.h:378-382` | `bool pure() const { return mode == Mode::Concrete && params.empty() && captures.empty() && ambient.empty() && anyOf.empty() && !allocates && !external; }` | 签名**每个部件都空**：`params`/`captures`/`ambient`/`anyOf` 全空、不分配、不外部。**只要读进签名就不纯** —— 与左边那条不是一个判据（这就是"两份真相"的现场：同一句 `pure` 的声明，一处说"没写"，一处说"没访问"）。 |

### 8.2 `lane/two-bits-model` 上"承诺的四个部分"怎么拼（原文）

`cffdb3de2e`（`sem: the signature's pure is the same promise as the record's, read once`）新增**一处判定的内联函数**，在 `src/kernel/include/camel/core/resource_semantics.h:427-441`：

```cpp
/// What a `pure` clause promises, in four parts, decided once: nothing is written, every access is
/// to a resource of the program's own, nothing is allocated, and nothing outside the program is
/// depended on. Both statements of the promise read this and only this -- the declaration's
/// (`Record::pure`) and the signature's (`ResourceSignature::pure`) -- so a reader cannot find two
/// answers to one question.
inline bool
keepsPurePromise(bool writes, bool accessesOutsideProgram, bool allocates, bool dependsOnOutside) {
    return !writes && !accessesOutsideProgram && !allocates && !dependsOnOutside;
}
```

两处 `pure()` 改成给它**喂四个布尔**（`cffdb3de2e:src/kernel/core/resource_semantics.cpp:606-615`、`:src/kernel/ops/include/camel/ops/operator_record.h:481-491`）：

```cpp
// 签名侧：把签名自己的部件翻成四个布尔
bool ResourceSignature::pure() const {
    return mode == Mode::Concrete && keepsPurePromise(
                                         writesSomething(*this),
                                         !ambient.empty(),
                                         allocates.has_value(),
                                         external && ambient.empty());
}

// 记录侧：把子句翻成四个布尔（第二格是 d892f35d72 加进来的"程序自己的"）
bool pure() const {
    return sem::keepsPurePromise(
        !uses.stated() || declaresWrite(),
        accessesOutsideProgram(),
        uses.stated() && uses.value.allocates.has_value(),
        uses.stated() && (uses.value.externalEffect || uses.value.functionOperand));
}
```

`d892f35d72`（`ops: reading is not writing, but what a pure clause reads still has to be the program's own`）加的就是第二格那个部件，`Record::accessesOutsideProgram()`（该枝 `src/kernel/core/operator_table.cpp:1275-1290`）：

```cpp
bool Record::accessesOutsideProgram() const {
    if (!uses.stated()) {
        return false;
    }
    for (const camel::core::sem::ResourceAccess &access : uses.value.accesses) {
        // Reading a file or the console is `mode Read` too, so "no write" alone would call such an
        // operator pure. The resource has to be the program's own: one whose class is not outside,
        // or one the declaration makes `readonly` ...
        if (camel::core::sem::reachesOutside(access.resource.cls) && !access.modifiers.readonly) {
            return true;
        }
    }
    return false;
}
```

### 8.3 读到之后的结论（对 §7 第 1 刀的落点）

1. **`keepsPurePromise` 的四格是要的形状**：承诺 = 不写、访问都在程序内、不分配、不依赖程序外 —— 已经是**一处判定**。
2. **但那枝仍留了两份拼装**：`ResourceSignature::pure()` 自己把 `!ambient.empty()` / `allocates.has_value()` / `external && ambient.empty()` 翻成四格，`Record::pure()` 自己把 `!uses.stated() || declaresWrite()` / `accessesOutsideProgram()` / … 翻成四格 —— 两边各拼一次。这正是决策者说的"要被删的两份真相之一"。第 1 刀要做的是：**`declaresNoWrite(...)` 只留一份拼装**，两个读者各自只交出自己那份事实（记录：子句；签名：测量出的部件），四格的拼装与判定不重复。
3. **`accessesOutsideProgram` 不新增为记录的第二个方法**：它是同一句"读的必须是程序自己的"在记录侧的又一次拼装；若保留，只能作为 `declaresNoWrite` 的**输入之一**，不许再自成一份判定。
4. **pre-publish 自检那一笔已被取代**：`bbed9dd32b`（`pure && (writes || allocates || externalEffect || functionOperand)` ⇒ 拒绝，用 `std::any_of` 内联问 `sem::writes`）后来由 `lane/c4-round-source` **改动 `63384055bc`**（枝尖 `ae14f65247`）改成 `sem::writesSomething(sem::signatureOf(r, 0))` + 文案 `a pure operator writes a resource`。第 1 刀以那一版为准（它是合并位那一批），`bbed9dd32b` 只作形状参考。

## 9 第 1 刀的调用面清点（落枝前就绪；`git grep -n "\.pure()\|->pure()" -- src` 全量 = 30 处代码 + 2 处文档注释）

**这里有四个不同的 `pure()` 答案，不是两个** —— 第 1 刀只合其中 2、3 两处，别的**一律不动**：

| # | 定义 | 读者（全部） | 第 1 刀动不动 |
|---|---|---|---|
| 1 | `AST::ResourceClauseLoad::pure()`（`src/parse/include/camel/parse/ast/type.h:254`，直接读 `pure_` 字段）—— **源码里写的那个词** | `gct/builder.cpp:1214`、`:1218`、`:1222`、`:1415` | **不动**（它就是"声明"本身） |
| 2 | `ops::Record::pure()`（`operator_record.h:487-492`） | `introspect.cpp:284`（`clause_pure` 列）、`facts.cpp:1324`（`Arrangeable::pure` 这一项） | **改成调 `declaresNoWrite(...)`** |
| 3 | `sem::ResourceSignature::pure()`（`resource_semantics.h:378-382`） | `builder_diagnostics.cpp:200`（`func->resources()`）、`:242`、`:284`、`:1075`（`promised`）、`builder_modules.cpp:421`（`signature`）、`:558`（`written`）、`builder_resources.cpp:500`（`at->site.clause`）、`builder_when.cpp:2291`（`signature.resources()`）、`resource_semantics.cpp:618`（`target`）、`:802`（`signature`）、`type/text.cpp:104`、`:318`、`:321`、`test/commit_checks.cpp:298`（`type->resources()`） | **改成调 `declaresNoWrite(...)`** |
| 4 | `runtime::fact::EffectSummary::pure()`（`effects.h:40`，`!external && !writesVariables`） | `execute/pass.cpp:1052`、`stdlib/graph/read.cpp:591`、`:599`、`std/cml/cml.cpp:1429`、`:1434`、`test/block_body.cpp:416`、`tiles/src/compiler.cpp:461` | **不动**（实测摘要，不是承诺） |

另有 `CppRegistration::pure()`（`operator_record.cpp:68`，builder DSL 的登记位）与两处文档注释（`operator_table.h:1028`、`operator_record.h:639`），都不是判定。

**名字核对**：`git grep -n "operatorPure" -- src` **退出码 1，这个名字不存在**；`operatorArrangeable` 有 6 处出现：定义 `facts.cpp:1533`、声明 `arrangeable.h:92`、读者 `runtime/graph.cpp:231`、`compile/src/gir/builder_helpers.h:769`、`test/block_body.cpp:509`、`test/facts_compare.cpp:229`。第 1 刀**不碰** `operatorArrangeable`（第五项是第 2 刀）。

**落枝即改的两行（形状；命名以决策者 §7 的 `declaresNoWrite` 为准，`two-bits-model` 里叫 `keepsPurePromise`，同一形状）**：
- `Record::pure()` → `sem::declaresNoWrite(!uses.stated() || declaresWrite(), /*accessesOutside=*/<记录侧那一格>, uses.value.allocates.has_value(), uses.value.externalEffect || uses.value.functionOperand)`（未声明子句的保守读法保持今天的样子）。
- `ResourceSignature::pure()` → `mode == Concrete && sem::declaresNoWrite(writesSomething(*this), !ambient.empty(), allocates.has_value(), external && ambient.empty())`。
- 实现**一处**：放在 `resource_semantics.h` 紧邻 `writesSomething`（`:422` 那一带），inline，四格全非才算守住承诺。

## 10 换底前的基线读数（第 1 刀"新增红 0"的对照；**底 = `625bfa13dc`，`src/` 等于 `integration@4f59be8265`**）

> 这一组是**换底前**的现场，第 1 刀落枝后要按**同一选法**逐名比；比的是名字，不是数字。读数在宿主 `88zjwei` 上取，`[provenance] tip 625bfa13dc…`。

- `test/plans/feat/tools/facts.plan.toml`：**11/11 通过，0 红**。
- `test/plans/feat/tools/facts_compare.plan.toml`：**2/2 通过，0 红**。
- `test/plans/feat/modules`（整目录）：`[SUMMARY] total: 2096, pass: 2068, fail: 20, skip: 4, known failures: 4`。
  - **20 条 FAIL（换底前的红，逐名）**：`op_stats.table.nvm`、`op_stats.table.fvm`、`export_unsupported_op`、`export_impure_op`、`pass_export_refusal_located.request_in_arm_json`、`as_refusal.constant_fails.pass`、`as_refusal.constant_fails.operator`、`slice_runtime`、`si_as_fails`、`si_as_fails_fvm`、`si_as_fails_jit`、`si_as_fails_prl`、`fusion_group.profile.none`、`npy_generic_wrapper_mismatch`、`rename.env_graph_identity`、`rename.process_graph_identity`、`types_and_node_kind`、`node_kinds_match_the_graph`、`outline_delivers_the_graph_it_makes`、`outline_delivers_a_graph_the_bytecode_vm_can_compile`。
  - **4 条 XFAIL（已知失败，不是红）**：`second_order_refused_vjp`、`ort_loop_values_dynamic`、`conv2d_shaped.derived_input_states_the_input_extents.nvm`、`conv2d_shaped.kernel_builds_what_it_derived.nvm`。
  - 第 1 刀后的判据是：这 20 条**一条不多、一条不少**（新增红 0），4 条 XFAIL 不动。
  - **20 条 FAIL 按 plan 归类（只归类，不解释）**：

| plan | 条数 | 名字（原样） |
|---|---|---|
| `test/plans/feat/modules/onnx.plan.toml` | 5 | `export_unsupported_op`、`export_impure_op`、`pass_export_refusal_located.request_in_arm_json`、`as_refusal.constant_fails.pass`、`as_refusal.constant_fails.operator` |
| `test/plans/feat/modules/tensor/shape_inference.plan.toml` | 4 | `si_as_fails`、`si_as_fails_fvm`、`si_as_fails_jit`、`si_as_fails_prl` |
| `test/plans/feat/modules/tensor.plan.toml` | 2 | `op_stats.table.nvm`、`op_stats.table.fvm`（plan 里是 `name = "op_stats.table"`，`:1295`，按后端展开） |
| `test/plans/feat/modules/rename_identity.plan.toml` | 2 | `rename.env_graph_identity`、`rename.process_graph_identity` |
| `test/plans/feat/modules/graph.plan.toml` | 2 | `types_and_node_kind`、`node_kinds_match_the_graph` |
| `test/plans/feat/modules/graph_outline.plan.toml` | 2 | `outline_delivers_the_graph_it_makes`、`outline_delivers_a_graph_the_bytecode_vm_can_compile` |
| `test/plans/feat/modules/tensor/bounds.plan.toml` | 1 | `slice_runtime` |
| `test/plans/feat/modules/tensor/dtype.plan.toml` | 1 | `npy_generic_wrapper_mismatch` |
| `test/plans/feat/modules/fusion_group.plan.toml` | 1 | `fusion_group.profile.none` |
| **合计** | **20** | |

  - **两条 `graph_outline` 的红已改判**（原先"与 `lane-editview-commit` 同族、单独盯"的猜测**被读数证伪**，下一个人别再按那个方向查）：这两条的断言是 `expect_stdout_exact`（`graph_outline.plan.toml:33` / `:43`；`expect_exit = 0` 那一侧**没报差异**），两侧输出**都是 633 字符**，只在 `offset 194` 差**一位数字**（期望 `new_id=81`，实得 `new_id=82`），`stderr` 空 ⇒ **"造出的图进了提交"这一侧本来就是对的**（`in_committed=true`、`graphs=1->2`、`made_graph=subject$outline6 nodes=3 exit_reads=2` 全对），红的是 **id 计数器漂了一位**：**不是交付缺陷，不修，也不改期望**。
  - 负责人原话（记在这里当同族假设的墓碑）：
    > "读数收到，而且它**证伪了我的同族猜测** —— 这两条不是'图没进提交'，是**期望把 id 的具体数值钉死了**……这与'提交把新图收进新程序'**不同族**，那条修完这两条**不会**变绿。"
  - 第 1 刀后照旧**逐名比**：这两条仍红、名字不变就**照实报、不解释**；若它们的 `new_id` 从 `82` 变成别的数（说明有别的位移），**单独点名**报负责人。
- **注意**：这组底不是最新 integration（`c4-round-source` 未落地；取这组读数时 integration 到了 `a5b8663744`，随后又到 `0fb33bc32f`）；换底后按 §7 的规矩**重取**一遍再把行号/读数改掉。

## 11 `rt::fact::Anchor` 带 `Program`：一条**钉子**，不是红→绿（决策 `D-1128-205` 批的 (A)）

**先看现场，再定判据。** 这条教训写在这里给下一个接这条的人：写用例**之前**先量"改前"的现场——否则很容易造出一条"改前就绿"的用例冒充红→绿。本次正是这样：决策者给判据时假设它会红，量出来它已经是绿的，于是**不做 (B)、不编红**。

**决策者更正（原文，要害）**：

> "我上一条说'换一个 `Program` 就看丢没改的图上的事实'是错的：`Stamp` 按闭包里每张图的 `(id, revision)` 算，提交又保 id 与 revision，所以看不丢；**`Program` 进键只是设计上多余**。"

⇒ 页里与所有后续报告**不再写"会看丢事实"**；这一项的性质是**设计上多余的一份身份**，是**清理**。

**改前现场（探针读数，`origin/integration@0fb33bc32f`，插桩跑完已复原、临时枝已删）**：

```
[SUMMARY] total: 12, pass: 11, fail: 1        ← 唯一 fail 是故意 assert(false) 打印读数的探针条目
FACTS probe third_f_id kept ; FACTS probe third_f_revision kept ; FACTS probe third_main_revision new
>>> FACTS probe_two_commits_f valid ; FACTS probe_old_program_f valid ; FACTS probe_same_program_f valid
```

机制（所以绿）：`Stamp` = `closureRevision` 的哈希（根 id + 闭包里每张图的 `(id, revision)`，`reachable.cpp:43-56`），提交复制图但保 id、保 revision（`draft_session.cpp:1935` → `inheritRevision`，`graph_build.h:177-181`）⇒ 换一个 `Program` 算出同一个哈希。

**钉子落在哪**：`test/plans/feat/modules/graph_facts.plan.toml` 的新条目 **`two_commits_keep_a_fact`** + `test::facts`（`src/stdlib/test/facts.cpp`）。**不落 `tools/facts`**：那里跑的是新框架 `camel::facts`，它的 `Anchor` 从来只有 `{graph, node}`；病在 `camel::runtime::fact::FactStore`，它的自有计划就是 `modules/graph_facts`、自有 pass 就是 `test::facts`。条目与代码注释里**逐字写明"这是钉子，不是红→绿"**，并把上面那行探针原文当来由记着（`graph_facts.plan.toml` 条目上方注释、`facts.cpp` 该段落注释）。

**判据（改 `Anchor` 那次才算全）**：`git grep 'Anchor{.*program()' src` = 0（**改前实测 2 处**：`src/stdlib/graph/facts.cpp:65`、`src/stdlib/tiles/src/compiled_scope.cpp:37`）+ 钉子绿 + `facts` 11/11 + `facts_compare` 2/2 不变 + `BUILD_EXIT=0` / `dirty 0` / `numstat`。

**钉子自己的读数**（branch `lane/fact-anchor-pin`，基 `0fb33bc32f`，tip `30521fbe3f`）：`graph_facts.plan.toml` `[SUMMARY] total: 12, pass: 12, fail: 0`；`facts` `total: 11, pass: 11, fail: 0`；`facts_compare` `total: 2, pass: 2, fail: 0`；`BUILD_EXIT=0`。