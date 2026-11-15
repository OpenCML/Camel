# 排队的工作项（非优先级一，但已由裁定落到纸面）

## 1. `(d1)` 的删除要改走**编辑接口**，不能在 SSA 这一路另写一份

决策者 2026-11-15 答复（`docs/handover/reviews/q-2026-11-15-should-the-deletion-come-before-the-carrying.cn.md`）里 `(d1)` 的验收原文：

> 只删确认没有读者的节点，**删除走编辑接口**（同日"谁接替被遍取走的持位节点"的裁定），**不在 SSA 这一路另写一份删除**。

已落的 `8d2fa8b8f` 里有一个 `dropCarriedVariableUnits`——**那是一份在 SSA 路上新写的删除**，正是不许的那一种。

**它当时不知道这条裁定**（裁定在它之后到达），所以不是违规，是**待改**。

改法有两条路，选哪条要按「位置交接收进编辑接口」那件事的进度定：
- 若编辑接口已有 `eraseNode`/`replaceNode` 的承接能力 ⇒ `dropCarriedVariableUnits` 改成调用它们；
- 若还没有 ⇒ 先把那个能力补进编辑接口，再改这里。**不要为了这一处单独造第三个删除入口**。

判据不变：删完四后端输出逐字不变，三个度量按下面第 2 条。

## 2. **「必须先删」不等于「删了就绿」——必要性不是充分性**

我在问页里写的是车道给的结论：「三项在 `(d)` 删掉无读者的 `:var/new` 与 `:op/assn_l` **之前不可能变绿**」。

**那是一个必要条件。我把它当成了充分条件来写**，于是决策者的验收口径写成「三个度量转绿后报 3/4」。

**实测是 1/3**（`8d2fa8b8f`）：只有 `var.an_arm_assignment_is_the_branch_result` 转绿；另两项的红另有原因——

> `a_loop_assignment_is_the_loops_state` 与 `loop_write_outside_read_is_the_let_writing`：**循环体里那次读还在读 cell**（`total = total + 1`），所以它**不满足「无读者」判据，删不动**——它要的是循环搬运。

所以正确口径是：
- `(d1)` 后：**1/4**；
- `(c)` 循环搬运后：**3/4**；
- `(d2)` 按值捕获后：**4/4**。

**这条错在措辞上，代价在排期上**：若照「3/4」去验收 `(d1)`，那一笔会被判成不合格，而它其实做对了。

## 判据（给后来的人）
写这种顺序结论时，**把「不可能早于」和「一定晚不了」分开写**：
- 「X 在 Y 之前不可能变绿」= 必要条件；
- 「做了 Y，X 就会绿」= 充分条件，**必须有实测才能说**。

## 3. 一处提交标题说的机制，diff 里没有（2026-11-15 记）

`origin/fsh-dev` 的 `f257714fe` 标题写着：

> Merge develop: a read after a branch takes the JOIN's value, **an unread unit is dropped through the edit interface**

而 `6f686ac40..f257714fe` 的改动文件里，**与编辑接口相关的 0 个**：

```
src/compile/gir/builder.{h,cpp}
src/compile/gir/builder_control.cpp
src/compile/gir/builder_expr.cpp        ← 191 行，删除实现在这里
test/cases/compile/var/arm_var.cml
test/plans/feat/compile/var.plan.toml
test/plans/feat/parse/golden/ast_hashes.txt
test/tools/writing-freedom.mjs
```

**实际做的正是本文第 1 节记的那件事**：`dropCarriedVariableUnits` 在 SSA 路上新写的一份删除。

### 为什么记它
**一个合并标题是别人读这笔改动时最先读到的东西。** 标题说「走编辑接口」，而代码没走——**两种可能，处理方式不同**：
- 若这是一句**期望/待办**（「应当走编辑接口」）⇒ 标题误导了事实；
- 若这是作者的**判断**（认为现有实现已经算走编辑接口）⇒ 那我第 1 节的判断错了，需要作者指认编辑接口是哪一个。

**在澄清之前，任何一方都不许按标题去改代码**——否则会为了对齐一句话而造出真正的第二个删除入口。

**判据**：这件事以「代码里删除从哪条路走」为准，不以标题为准。第 1 节的待改项保持原样。

## 4. 返工：`f883c88ee` 的第 4、5 条要撤（决策者 2026-11-16 凌晨裁定）

裁定原文在 `docs/handover/reviews/questions-2026-10-29.md` 末尾「返工」一节。要点：

- **第 4 条（补回 `IgnoredSideEffect`）要撤**：让循环状态「声明自己写了」会让它报「缺 `sync`」。按原则 3.1 与 11-15 的答复，**`IgnoredSideEffect` 对 `var` 的两支要删，不是要保住**。改的是 `var_read_before_write` 的期望（去掉 `IgnoredSideEffect`）。
- **第 5 条（补回 `COPY → LOOP` 顺序边）要撤**：新语义下 `var` 不是资源，**变量的写之间没有顺序边**，先后只靠数据依赖。`order_edges_regions.gir` 里那一条从金标去掉；同一用例里**对外部资源**的顺序边保留。
- 第 2、3、1、6 条（循环只露出头部声明的状态；无 store 的单元也能删；按函数锚点定位；「认领」与「交出值」分开验）**没有问题，保留**。

### 这件事的形状（比返工本身重要）
我把第 4、5 条写进合并正文，并称它们为「今晚第三次同族」「折叠只搬它承认的那一类边」——**把重建旧机制读成了深刻的一课**。

真正的病不是纪律松，是**判据缺**：「不许改期望」这条规矩单独存在时，**遇到「新语义下旧用例变红」，它只能把我推向「去把旧机制补回来」**。

判据已写进 `agents/test.md` 末条：**先问那条用例测的是不是已撤回的规则。**

## 5. 生成文档里的示例已经不再示范那条诊断（2026-11-16 记）

`diagnostics/examples.tsv` 仍把 `SemanticDiag::IgnoredSideEffect` 的示例钉在 `warnings_visible.cml`，**而那个例子现在已不再触发这条警告**（臂里直写 `var` 按 3.1 不是效应）。

`docs.links.diag_catalog` 的 `doc_is_generated` **是绿的**——因为它比的是**页面正文与源码是否一致**，不是**示例是否还示范了这条诊断**。

同源的还有一处：该诊断的 help 文字仍是旧读法（「A write outside one is ordered after the reads written before it…」）。

### 要做的
1. 给 `IgnoredSideEffect` 换一个**真的会触发它**的示例（`reportIgnoredCapturedWrites` 那一支仍活着：被调函数捕获写、调用处没交回）；
2. 改 help 文字，去掉「顺序」那半句（新语义下 `var` 的写之间没有顺序边）。

### 判据
**「示例能过检查」与「示例示范了这条诊断」是两件事。** 检查只证明前者。**改完之后要有一个读数能证明后者**——例如把该示例的诊断码与它出现在目录里的用途对上，或让检查在「示例不产生该诊断」时报红。

**在做到这一步之前，`IgnoredSideEffect` 的目录页会一直是绿的，而它是错的。**

## 6. 诊断示例是按名字相似度自动钉的（2026-11-16 记，第 5 节的成因）

`node scripts/diagnostics-catalog.mjs --write` 在**还没有用例钉住**某条诊断时，**按名字相似**挑一个用例当示例。实测：新诊断 `ClosureAssignsCapturedName` 被自动钉到 `err_closure_assigns_let.cml`，**而那个用例触发的是 `ImmutablePassedToVarParam`**——它不是新诊断的证据。

`--verify-doc` 当时是绿的，因为它只比**页面与 TSV 是否一致**，不问**示例是否产生那条诊断**。

### 所以第 5 节不是「一个旧示例烂了」，而是「生成器会系统性地产出这种示例」
每加一条诊断，就自动多一个「页面绿、示例不示范」的坑。这不是偶发，是默认行为。

### 要做的
1. `--write` 选示例时**先验证示例确实产生该诊断码**；验证不通过就**不钉**、留空并在页面上写明「待补示例」——**宁可缺一格，不要错一格**；
2. 或让 `--verify-doc` 增加一条：**示例用例的输出里必须出现该行钉住的诊断码**。

### 判据
**「页面与 TSV 一致」不是「示例示范了这条诊断」。** 前者可以用生成器保证，后者必须真的跑一次示例。

**在做到这一步之前，每加一条诊断都会自动多一个绿着的错页。**

## 7. 两处纪律事实（2026-11-16 记）

### 7.1 `node scripts/format.js` 会重排既有注释，产出几百行噪声
实测：`defunc` 那笔改 `defunctionalize.cpp` 与 `draft_inline.cpp`，格式化会把这两个文件里**既有注释整体重排 542 行**。

车道的处理：**还原格式化结果，只手抄回自己的块**，终态 diff 保持 `+360/−18`。

**为什么这条要记**：下一个人跑格式化时会看到同一片噪声，**而他会以为那是这次改动的一部分**。看到几百行注释位移时，先问「这是格式化还是改动」。

### 7.2 我的派工文本用了行号引用，违反本仓库的规矩
我在派工里写 `defunctionalize.cpp:346`、`:470`；`defunc` 按 `agents/commit.md`「引代码用符号不用行号」改成了 `path#symbol`，并把设计原则的 `160/167/154` 换成条目名。

**它用仓库规则压了我的指令，并且说明了理由。** 这是对的。

**同样的事今晚发生在我别的派工里**（`docs/design/effects/plan.cn.md:51` 的引证也是行号形态的红账）。**派工文本自己要先合规**——否则车道要么照抄一个坏例子，要么每次都要替我改正。

## 8. `(d1)` 的编辑接口缺口已补，但它新增的保证没有被任何用例钉住（2026-11-16 记）

`lane-editiface`（`0bdb37e38`）给 `GraphDraft` 加了 `eraseUnreadNodes(span)`：**判据与删除在同一个动词里**，删除仍只经 `eraseNode`（位置交接仍是 `closeChainOver`），所以**不是第三个删除入口**。`dropCarriedVariableUnits` 里「能不能删」的 3 处判据归零，只剩成员资格与走法。

### 它给了两个方向相反、但都正确的读数
- **把调用换回旧的逐节点删除 ⇒ 完全没区别**（3428/3408/0，var 仍 14/14，golden 逐字节）；
- **把 `eraseUnreadNodes` 改成一律 `return false` ⇒ var 12/14**，红的正是 `an_arm_assignment_is_the_branch_result` 与 `closure_read_is_the_let_writing`。

**两个读数指向不同的事**：前者说「判据一次都没行使」，后者说「调用确实在活路上」。**只做第一个会得出「改造无效」，只做第二个会得出「判据有效」。**

### 债
`eraseUnreadNodes` 唯一新增的语义是「判据失败则**整集一个节点都不动**」。而现有程序里**没有一个单元的节点还带着读者** ⇒ 它一次都没触发。

**所以 (d1) 在字面上满足（删除走接口），而那条保证是防御性的、未被验证。**

要回答的问题：**「一个单元的节点仍带着集合外读者」这个形状，在语言里构造得出来吗？**
构造得出来 ⇒ 补一个用例钉住「整集不动」；构造不出来 ⇒ 在文档里写清它在什么条件下才会触发，**不要让它显得像被验证过**。

### 两处已知未做（都不是遗漏，是范围）
1. `closeChainOver` 交不出去时**仍然静默**——裁定第 2 条要它失败，那是跨 pass 的契约变更（`placeLostStatements` 还在 fold 里），另开车道；
2. `collapseNonEscapingVariableReads` 删「没人用的 read」仍直接 `eraseNode`——**同一类判据的第二个、也是最自然的调用者**。

## 9. 我写错了一个前提：`ClosureCaptureEscapes` 不是「另一条有效规则」（2026-11-16 裁定）

我在问答页里写下「两条规则都对，是优先级问题」，并据此建议「改载体、不让 D1 让位」。**决策者推翻了这个前提**：

> **这不是优先级问题：`ClosureCaptureEscapes` 已经不是有效规则。** 它是 2026-11-14 规则二「可变单元活不过那次求值、离开的闭包不能按引用捕获 `var`」的检查；11-15 定案「闭包捕获的是值」，**规则二整条撤回**（原则 3.5「闭包捕获的是值」一条写着「取代 2026-08-14 的规则二」）。**闭包捕获的是值，带着值离开作用域没有任何问题。**

**所以从来不是两条规则相撞，而是一条规则与一个已撤掉的检查相撞。** 我的「优先级」框架本身就是错的——正确的话是：**那条检查该删。**

### 裁定给的四件（属第 3 步，不单独派）
1. **删** `SemanticDiag::ClosureCaptureEscapes`、`builder_state.cpp` 里携出的判定（`carryOutOf`/`carriedOffAt` 一族）与它在 `docs/diagnostics.md` 的条目；
2. `state_escape/` 的 5 条：程序里写了捕获的名字，**按新规则报 D1，期望改成 D1**（不是改载体）；
3. **另加 5 条对照**：同样的离开方式（返回、放进字段、放进数组、交给会存它的函数、放进另一个闭包），**闭包只读**捕获的 `var`——它们合法、能编译能运行，读到的是闭包创建时的值；
4. 算第 3 步的工作量。

### 教训（写下来是为了下次更早问）
**我看到「两条规则」时，应该先问「后一条还活着吗」，而不是问「该谁先报」。** 一个已被取代的检查与一条新规则相撞，看起来完全像优先级问题——**因为它仍然在报错，所以看起来仍然有效。**

---

## 工具事实：`-Plan` 与 `-Changed` 同时给时，`-Plan` 被忽略

```
camel-remote.ps1 -Changed -ChangedBase <sha> -Plan @('a','b')      # 选中的是 --changed 自己的集合，a、b 不进
camel-remote.ps1 -Plan @('a','b')                                  # 要点名计划，就不要带 -Changed
```

**这条已经让两个人各误读一次读数**：一次把 `--changed` 自己选出的 6806 条当成「`--changed` 加上我点名的七个
计划」，一次把 `--changed` 选出的 148 条当成「四个族的最小闸门」。两次都不报错，只是**静默少跑**——
这正是「一条检查从不运行，和没有这条检查，读数上一模一样」的同一类错。

**判据**：报告的 `[SUMMARY] total` 与自己预期的数量级不符时，先看跑起来的 `[PLAN]` 名单，不要先解释读数。

## 新发现、未定责：`debugger.dap_*`

`slow` 层轮转抽到 `feat/tools/debugger`：`debugger.dap_session`、`debugger.dap_stepping` 两条红，
**与今晚的语义改动无关**（它们起于 DAP 会话与单步）。**先记不修，下一轮轮转再看**，不插进正在合的这一批。
## 10. `lane-byref` 报的两条缺陷（不归它修，记账）

### A：捕获一个 `Ref` 句柄并写它，被效应推断判成 `uses capture 0`
`std::opt` / `std::macro` / `std::defunctionalize` 下被拒：
```
error[RuntimeDiag::PassEffectContainment]: Pass 'std::defunctionalize report=false' returned a program
whose call site reaches past the type it declares: node 2 of graph 'f' (a call of 'inc', which is 'a')
declares uses capture 0; the graph it runs touches write state::Ref
```
最小复现（默认流水线跑出 `21`，带上优化就拒）：
```camel
import state
func f(): int sync {
    let x = state::ref(1)
    func inc(): int sync { state::set(x, state::get(x) + 1); return 1 }
    let a = inc()
    return state::get(x) * 10 + a
}
```
**这是实现缺陷，不是语言规则**：写穿捕获的句柄被记成 `uses capture 0`，而它实际触碰 `write state::Ref`。修法在 `decideCaptureByValue` 之后的效应推断。
**用例的绕法（句柄当实参传）是正当的**——用例不该为实现的缺陷买单，`captured_write_order.cml` 的注释已记下这个形状。

### B：非 `sync` 体里 `set` 单独成句，写被丢掉
```camel
func p(): int {                  // 非 sync 函数体
    let c = state::ref(0)
    state::set(c, 1)             // warning: EffectNotExecuted
    return state::get(c)         // 印 0
}
```
同文件里 `println(...)` 在非 sync 体里**照常执行**；`let v = state::set(c, 7)` 其值被用到时也照常执行（`v + get(c)` = 14）。只有「`set` 单独成句 + 后面有读者」这一形状丢了写。

**按原则 3.3 的字面它该被执行**（「对 `Ref` 的写由之后的读者用到（3.1）」）——设计原则与代码冲突时原则优先，**所以这是缺陷，不是待裁定**。它直接决定了 `lambda_capture_write` 为什么必须改成 `sync`：原用例「后置的读把写拉活」那个机制**对 `Ref` 不成立**。

### 教训
**同一条 `Ref` 语义，在 `var` 上成立的「后置的读把写拉活」在 `Ref` 上不成立**——迁移时最容易漏的正是这种「只换了载体、没换机制」的地方。`byref` 认出了它并只报不改，是对的。

## 11. 作者把两条 `Ref` 缺陷升到第一档（2026-11-16）

`origin/fsh-dev` 的合并提交 `704ff582a` 标题原话：

> **`Merge develop: by-reference capture cases migrated, the var unit deleted through the edit interface; put the two Ref defects into priority one`**

**所以第 10 节记的 A 与 B 不是「顺带记一笔的债」，它们是第一档的一部分。** 已派 `lane-refdiag`（只诊断不改源码），要求它给出：同一根因还是两个缺陷、修法的 `file#Symbol`、**预测炸开范围（标注预判）**、可证伪判据、以及**风险**——尤其「修完之后会不会有写从『不执行』变成『执行』」。

**作者把指令写在合并提交的标题里，没有改 `docs/roadmap.cn.md`。** 这一点要记住：**他的裁定以提交到达 `origin/fsh-dev`，载体可以是标题、可以是 `docs/handover/reviews/` 下的页——不一定是任务书文件。** 巡检时必须读 `origin/fsh-dev` 的最新一笔标题，不能只看 roadmap 有没有变。

## 12. `lane-refwrite` 撞出的两条（都超出它那条放宽的范围）

### C1：`if` 语句的值没人要时，**整支不运行**——连外部写（`println`）也被丢
```camel
func p(): int {                  // 非 sync 函数体
    if true then { println("inside") }
    return 0
}
```
**基线就什么都不印**，并报 `EffectNotExecuted`。`state::set` 在同一位置也一样。

**按 3.1「区域因为它的 owner 运行而运行」，这是缺陷**：`if` 语句的 owner 是函数体，函数体运行了，它就该运行，臂里的外部写就该发生。**实现里「一条值的没人要的分支语句」被整支跳过。**

- `ref.plan.toml` 的 `dead_loop` 把**循环**那一半钉成「不运行」——那是另一件事，**别混进来**；
- **分支这一半没有用例**，所以它从来没被问过；
- 对**外部写**与**句柄写**是**同一个洞**。

### C2：「循环体里读写外层 `Ref`」在基线上就是坏的
```
loop 体里一个被 println 拉活的 state::get(outer_ref)   → 印两轮 box 0 后
                                                        RuntimeDiag::InvalidArgument @ state.get
```
**与写无关，只读就崩。** 怀疑正是「捕获端口被当成形参」那一族（`lane-portbind` 在修 `src/runtime/resources.cpp`）。

`lane-refwrite` 的放宽让**写**那条路也能走到这个洞：`state::set(outer_ref, …)` 在循环体里从「被丢掉、印 0」变成「第 0 轮就崩」；连 `sync { state::set(c,5) }` 写进循环体也崩。

**所以 `lane-refwrite` 的放宽不合主干，等 `lane-portbind` 落地后重跑 `R3`。** 作者认的代价是「多执行一次不可观察的写」，**不是「崩」**——这一条界线要守住。

### 一条方法上的记认
`lane-refwrite` 在「修完反而更坏」的地方**停下来报**，而不是提交一个「六条里四条达成」的交付。**这是对的**：`R3` 从「印 0」变「崩」是可观察的退步，而验收表是作者的——**没人有权把一条验收改成「不适用」来让一笔提交变完整。**

## 13. 作者答了 `Ref` 写那页：**同意默认**，并改了原则（2026-11-16）

`origin/fsh-dev`（`3df4ac383` 带来 `reviews/q-2026-11-16-may-a-ref-write-that-nobody-reads-be-dropped.cn.md`）原话：

> **同意默认：对句柄（`Ref` 与其他 `#resource` 类型）的写由所在区域用到，与外部写同一条规则。** 句柄的读者可能在调用方、在之后被调用的闭包里、在另一个同类型的名字后面，本图判不全；条件式保活会把 `R6`/`R7`/`R8` 判成可删。**代价是一次真没人读的句柄写会执行一次，不可观察，接受。** 调试器「从不执行的行不是停点」那条用例**按新规则改期望**（句柄写的那一行现在会执行）。
>
> **原则已改**（3.1「对外部资源与句柄的写由所在区域用到」、3.3 把「内部资源」一条改成「句柄」、3.3「要执行的效应之间按书写顺序」同步），**取代 11-15 写的「对 `Ref` 的写由之后的读者用到」**。

### 一个必须记住的结构性事实
**原则文本的修改在 `origin/fsh-dev` 上，不在 `origin/develop` 上。** 作者的流程是把 develop 合进 fsh-dev，所以 `docs/design-principles.cn.md` 在我们这条线上**永远是旧的**。

**所以任何车道读原则时，凡涉及最近裁定的段落，都要去 `origin/fsh-dev` 读那一笔**：
```
git show 07c9b4358:docs/handover/reviews/…      # 更早的答复
git -C <repo> log -1 --format=%s origin/fsh-dev  # 最新一笔标题里可能就有裁定
```
**这不是绕过原则，而是原则的最新文本存在另一条线上。**

### 同轮并入第一档的两件
1. **非 `sync` 函数体里值没人要的 `if` 语句整支不运行，连臂里的 `println` 也不执行**——违反 3.1。已派 `lane-ifarm`（见第 12 节 C1）。
2. **循环体里读外层 `Ref` 在基线上就崩**——作者确认顺序：**先修捕获口（`lane-portbind`），再放宽（`lane-refwrite`）**。
