# 问：遍能不能在发布「体」的同一次 commit 里就把「型」补上，还是要一小块新 API？

`2026-11-18`｜车道 `lane-gpu`｜基线 `origin/develop = bdf5e0adb`｜引 `075a2bddf`（甲/乙两条诊断上线）、
`eccc6824a`（分档从「构建是不是 Release」改成「这次编辑加了什么」）、`7a70b5fad`（同一个缺陷的第一次修法）

## 一、为什么必须问

`gpu-plans` 这一层在 `bdf5e0adb` 上红 **32** 条（`total=146 pass=114 fail=32 skip=0`，两次读数逐字相同）。
其中 **30 条**落在同一个时间差上，**2 条**是它自己的老红（`static_loop_device` / `multi_state_loop_device`
的比特保持断言，脚本先 `assert returncode == 0` 已经通过），见页末。

30 条的形状是同一句话：

```
'tiles::compile' committed a graph that reaches past the effect its own type declares:
node <n> of graph '<g>' (tiles:download / tiles:upload / tiles:run_compiled),
in a graph whose type declares <nothing | uses var stdout>,
makes an access the type does not cover (write tiles::driver | read tiles::device)
```

**⇒ 图是遍自己刚造出来的图。** `tiles::compile` 把 host 体搬去设备，体里多了 upload / download /
段运行器；这些算子写 ambient `tiles:driver`、读 foreign `tiles:device`（`modules/tiles/src/module.cpp`
的 `declareResources`）。而**型还是 host 体那一个**（`nothing`，或 `uses var stdout`）。

## 二、让它成立的读数（这是问句的读数，不是猜测）

1. **哪个 commit 拒的，已经钉死（不是推的）。** 在 `RuntimeGraphDraftSession::commit` 的
   `throw GraphCommitRefused` 前打临时探针（`backtrace_symbols_fd`），跑
   `test/plans/gpu/tiles_regions.plan.toml`，调用链是：

   ```
   RuntimeGraphDraftSession::commit(bool)
   ← camel::tiles::outlineRegions(...)          # modules/tiles/src/region_outline.cpp:598
   ← (tiles 遍)  ← applyPassesDetailed
   PROBE commit-refused graph='constant_axis' made=4 changed=3 checks=0
   ```

   **⇒ 是 `outlineRegions` 自己的 `session.commit()`，不是别处的会话。** 顺带量到
   `commitChecksEnabled() == 0`（Release 就是把这一格关着的）：**这条拒绝与构建档无关**，
   它是 `commitViolation` 的「这次编辑加了访问 ⇒ 问」那一档（`eccc6824a` 的裁定），
   **每一个构建都会问**。这条纠正过一版报告：它曾经被归到 pass 边界的
   `PassEffectContainment` 那条线上，不准确——探针给的 `checks=0` 与「拒绝照旧发生」同时成立，
   只有 commit 这一档能解释。
2. **量体必须在一张「整批重新绑定」的程序上，这是本页最硬的一条约束。** commit 的编码顺序是
   `committed->bind(callee.id, calleeCopy)` 先全做完，再按逆序 `encodeDraftGraph` 逐个编码
   （`draft_session.cpp` 的 `allocate`/`encode` 两个 lambda）。也就是说**只有 commit 手里那份
   `committed` 是「所有草稿图都已换过的程序」**；任何「拿草稿 + 现在的程序去量」的读数，在解析
   调用时看到的都是**旧的**被调图，于是量小了、`covers` 说盖住了、加宽什么也不做。
   这正是 `bodySignature(program, const GraphDraft&)` 这个载重**过不了它自己的相等性检验**的原因：
   它对一张被调图也被改过的草稿，给出的签名与 commit 量出来的不相等。⇒ 片面的返回值是
   「遍不能用它来加宽」，而不是「加宽是错的」。
2. **顺序是「先发体、后补型」。** `region_outline.cpp:598` 把带传输节点的体提交发布；
   `region_pass.cpp:82` 的 `settleDeclarations` 随后（下一批 commit）才把型加宽到体上。
   两者的注释写的也正是这件事：「The type is what a caller reads (analysis 4.1), so it is brought up to
   the body here -- by this pass, which is the one that changed the body -- and never by widening the check.」
3. **同一个缺陷 2026-11-15 已经修过一次，而且修的就是这几个用例。** `7a70b5fad`
   （`fix(tiles): bring the type over a device body up to what the body does`）名下点名的三个用例正是
   `nofallback.branch_segment_is_written_in_the_arm`、`nofallback.branch_scope_returns_what_the_host_returns`、
   `resident_loop_declares_its_blocks`，图名正是 `choose`；那一笔的提交信息明写
   「The check, its width, the GPU plan assertions and the exemptions are untouched」——
   **当时的裁定是：该补的是遍，不是检查。** 今天这一条读数把那三个用例又打红了。
4. **遍现在补不了 —— 这是问句的核心。** 想「在发布前加宽」，就得先量出草稿体的 footprint；
   而三个读数器都要一张 `GCGraph`：

   ```
   runtime::footprintOf(const Program&, const GCGraph*)      include/camel/runtime/resources.h:120
   runtime::bodySignature(const Program&, const GCGraph&)     include/camel/runtime/resources.h:321
   runtime::widened(declared, body)                          include/camel/runtime/resources.h:327
   ```

   草稿不是 `GCGraph`：`GraphDraft` 只有 `encode(...)` 之后才是，而 encode 是会话自己
   （`GraphDraftGroup::encode`）在 commit 里做的一步，不是遍能提前调的。**实测过**：把
   `settleDeclarations` 的 widening 循环原样搬到 `session.commit()` 之前，编译通过，
   `32` 条读数**一个字没变**——因为 `session.collectReachableRuntimeGraphs()` 给的是**编辑前**的图，
   `bodySignature` 量到的还是旧体，`covers` 说「已经盖住」，于是什么也没加宽。
   **⇒ 「先体后型」是当前 API 下唯一可行的顺序，而它正好被这个 check 拒。**

### 为什么遍不能「先把型建好再发布」—— 整件事的核心一句

同一张图上，**型只能在 encode 之前设在草稿上，体只能在 encode 之后量得到**：

```
GraphDraft::funcType()        include/camel/runtime/draft.h:436    ← 草稿手里有型
GraphDraft::setFuncType(...)  include/camel/runtime/draft.h:621    ← 而且只能在这里设
GCGraph::funcType()           include/camel/runtime/graph.h:839    ← 编好的图只有 getter，没有 setter
```

⇒ 于是「发布一个型和体都对的图」被夹在两句话之间：

```
要量体 ⇒ 必须有一张 encode 过的图 ⇒ 那时型已经随草稿编进去了
要改型 ⇒ 必须在 encode 之前       ⇒ 那时还量不到体
```

**⇒ 所以只有两条路**：要么在 commit 里走一个「编码 → 量 → 加宽草稿 → 重编码」的定点，要么给
**已经发布的图**一个设型的入口 —— **而后者是给检查开一个新口子**：任何调用者都能改一张已经发出去的图的型，
那正是这套检查在关的那类漏。**裁定 `f61196246` 取前者（定点）。**

**⇒ 还有一条边界，这一页必须写着**：只有 `made`、没有 `before` 的图（遍新造出来的，例如 `__g_50__`）
**没有「它继承的 clause」可比**，所以上面「base 本来在不在它自己的 clause 之内」这条可量事实不存在。
裁定把它留在**照旧拒绝** —— **没有可量的事实就不放宽**。它不在这条修法里，是本页之外的第二格活，
而它在 32 条里占几条（记作 N），就是这个缺口的尺寸。

## 三、三条候选路与各自的代价

```
(a) **遍在发布前自己加宽**（默认建议）。代价：需要一小块新 API —— 让 footprint 的读数器能读
    `GraphDraft`（`bodySignature(program, const GraphDraft&)` / `footprintOf(program, const GraphDraft&)`），
    或让会话提供「量一下这张草稿的体」而不发布。落点在 `src/runtime/resources.cpp` + 头文件；
    遍侧只需要照着它插入的算子把 clause 并上去（`sem::signatureOf(spec, withCount)` 与
    `runtime::widened` 都是公开的，这段在 modules/tiles 里能写完）。
    代价的性质：**它是加一条读数，不是放宽一条检查**，与 `7a70b5fad` 的裁定同向。
(b) **把甲退回 pass 边界**（那里已经问过一遍，`PassEffectContainment 0x040001AF`，`970185fa4`）。
    代价：`075a2bddf` 的整个立论「把两个问题放到 commit 上」被撤回；而 commit 是最早能拦下车的地方，
    退回边界意味着**一个把体改坏的遍可以先跑完**（它自己的返回值才被查）。
(c) **让 commit 知道自己处在「遍内」**（例如遍在会话上开一个「结算未完成」的标记，commit 见到就
    只记不拒）。代价：这是给检查开一个信任口子，而 `eccc6824a` 刚刚把「按编辑分档」立成规矩：
    下一个遍会照着这个口子写。
```

## 四、建议，以及裁定把它落成了什么

```
**按 (a) 走：让遍在发布体的同一次 commit 之前，把型加宽到体上。**
```

**⇒ 而且落地时不必新增任何 API。** 把这件事放到 commit 自己的那一步，就是它字面上的意思：
`commitViolation(*committed, *copy, ...)` 拿到 `*copy` 时，**体和型都已经编码好、互相可量**。
在这里，只要草稿没有把 clause 收窄（草稿的 clause 与它从 base 图继承的那一个相同，说明这一遍
只是改了体、没有改承诺），那么「体超出型」唯一诚实的处理就是**把要发布的那份副本的型加宽到它的体**
（`widened(declared, bodySignature(*committed, *copy))`），而不是拒绝。手写的、被作者收窄过的声明
仍然拒——检查的牙齿没有掉。
这条路的代价最小：不改 `footprintOf` / `bodySignature`，不教走查读草稿，也不给检查开口子；
它只是把「遍该补的型」补在唯一能同时看见体和型的那一步。

（原计划是 `bodySignature(program, const GraphDraft&)` 这样的只读重载。它写不出来而不说谎：
`FootprintWalk` 与 `Resolver` 是整套按 `GCGraph` 写的——源点、别名、端口、区域，逐个都要在
草稿上重写一遍；而按 §二.2 的「整批重新绑定」，还要把会话里**其它草稿**一起量进去。复制一份
走查去量草稿，就是这一页反对的那种「看起来对的读数」。）

### 四之一：裁定的落点，以及那三条可量事实（`f61196246` 已答）

**裁定：遍在发布那一刻声明它加宽后的类型。** 落点是 commit 自己的那一步 —— `commitViolation(*committed, *copy, …)`
拿到 `*copy` 时，**体和型都已经编码好、互相可量**。在那里，只要下面三件事**全都可量且成立**，
「体超出型」唯一诚实的处理就是**把型加宽到它的体**（`widened(declared, bodySignature(*committed, *copy))`），
而不是拒绝：

```
① 这一遍没有改承诺   copy->funcType()->resources() == before->funcType()->resources()
② base 本来守规矩   covers(before->funcType()->resources(), bodySignature(*committed, *before))
③ 没有 base 就不放宽 （只有 made 的图没有「它继承的 clause」可比 ⇒ 照旧拒）
```

**注意 `commitViolation` 的第二个参数不是 base**：它是编码后的新图，**第五个参数 `before` 才是那张 base 图**
（`heldBefore(graph)` 保证它在 program 里仍是原来那一个；契约见 `include/camel/runtime/resources.h:420-428`）。
①②因此都是只读的测量，**与「哪个遍在跑」无关**；手写的超承诺函数在 ② 上是 false，于是不走加宽、照旧拒 ——
检查的牙齿没有掉。

**⇒ 落地形状**：commit 里一个「编码 → 量 → 加宽草稿 → 重编码」的定点，不改任何公开 API。
（另一条路是给 `GCGraph` 一个设型入口，改动小得多，但它让**已发布的图**能被任意改型，是给检查开新口子，不取。）

**⇒ `N` 是这个缺口的尺寸**：③ 让「只有 made、没有 base」的图留在红里，那几条不在这条修法里。

## 五、这条红现在的实际范围（比 `gpu-plans` 大）

```
gpu-plans（report-only）       total=146 pass=114 fail=32 skip=0   30 条落在这一个时间差上
feat/modules/tiles（**门禁层**） total=109 pass= 77 fail=26 skip=6   11 个包含式拒绝点 + 同一个乙的点
```

`test/plans/feat/modules/tiles.plan.toml` 在 `feat/**` 里是要过闸门的，所以这件事不只是
「report-only 层有 32 条红」。

## 六、页末：不属于这一条的 2 条

`static_loop_device`（`loop_device.py:68`，「empty loop changes initial bits」）与
`multi_state_loop_device`（`loop_multi_device.py:110`，「swap1 must preserve exact initial bits」）
是 `internal/tools/gpu-plans.exceptions.txt` 里已经记着的两条「比特保持」老红：
两个脚本都先 `assert result.returncode == 0` 再比比特，而它们没有走到任何一条拒绝，
所以它们**不是**这一批的 30 条，也不该被这一条修好之后算成「已修」。

---

**已答（2026-11-21 移入 `reviews/`）**：答在 `questions-2026-10-29.md` 的「6 条红分两类，我的处置不同」第 2 条，以及紧接的「四、我为什么不合，以及顺序」（第 5371、5375 行，量于 `33d19e6ed`）：越界的是 `tiles::test_outline_*` 造出的 `__g_49__`，「型什么都不声明、体经 `chain` 带出程序外效应」，**生产者是 tiles 那一遍，不是前端**；处置是先修 tiles 那一遍，再把两笔一起合。正文不在此复制。
