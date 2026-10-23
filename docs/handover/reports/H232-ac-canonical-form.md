# 结合交换匹配的规范形：一页计划（H232 第 3 步）

依据：规则语言第二版设计稿 §2 与 §6（`docs/design/rewrite/v2.cn.md`）、算子语义协议 7b
（`camel/core/operator_semantics.h` 的 `BinaryLaw`）、决策者 2026-10-23 的默认决定（"结合交换匹配的
规范形是同一算子的项展平为按节点身份排序的多重集"）。本页只定结构，不重复设计稿的理由。

> 位置说明：本任务的禁区包含 `docs/design/**`，计划因此落在报告目录，请协调者并入
> `docs/design/rewrite/`（或在评审里指明它应为 `plan.cn.md` 的哪一节）。

## 决定

**规范形是匹配器读到的多重集，不是图上的新节点。** 一个声明了交换律与位精确结合律的算子
（`BinaryLaw.commutative && BinaryLaw.exactAssociative`，按元素类型）所构成的一串项，在匹配时被读成
"展平后按节点身份排序的多重集"；图本身不加新的节点种类、不把链改写成 n 元节点、也不把程序重写成
规范形状。由此得到的约束：

1. **读侧视图**：`DraftWritings::matchFrom` 在对某个候选节点做匹配时，把"同一算子、同许可、同元素类型、
   都参与取值（`takesPart`）、无 `with` 输入、无控制输入"的链展平成项的多重集；模式一侧同样展平。
   排序只用来让搜索确定、与程序写下的顺序无关。
2. **写下的顺序优先**：仍先按位置配对（今天的行为），失败后再按声明的交换律换序，最后才走多重集路径。
   一条把操作数按特定顺序写下的定律，在程序正好写了那个顺序时读到的仍是那个顺序。
3. **完全覆盖**：多重集的每一项都必须被某个模式项接住；模式项比项少时，只允许**恰好一个**"在模式里
   只出现一次的普通变量"（`const` 参数不算，重复出现的变量不算）接住剩下的项，模式项比项多则直接不匹配。
   于是没有一项会在定律没读到它的情况下被丢掉。
4. **剩余项被重新拼回一个值**：接住剩余项的那个变量在右式里读到的，是"把剩余项按同一个算子重新拼成的
   一条链"（新建一个链节点），而不是某个已经存在的子链。所以匹配**会**新建节点，只是新建的节点就是
   "剩下的项的和"本身，是同一个值，等价类不因此变大。`(x + 0) => x` 于 `a + (b + 0)` 得到的是新写的
   `a + b`；从库里删掉的重排规则不再需要，因为剩下的项本来就只有一种值。
5. **提取侧的同一件事**：同一个多重集也是**存储的键**——`DraftWritings::index()` 对一个声明了交换律与
   位精确结合律的算子节点，把它的项展平、按节点身份排序后作为 `NodeKey.norm`，于是"只差顺序或括号"的
   两种写法在 congruence closure 里就是同一个等价类，不需要任何规则。这就是设计稿 §6 里"抽取"那一半：
   交换律、结合律不再写成规则（库里删除 `*_comm`、`*_assoc` 与按操作数顺序重复的规则），饱和不再靠
   重排增长等价类，抽取在同样的代价模型上读到更少的写法——这是给 H226 对照测量的一条已知后果，不是缺陷。
6. **只有位精确的结合律进匹配器**：浮点加法/乘法、张量算子没有声明 `exactAssociative`，它们只得到
   交换匹配（今天已有）；浮点求和的重新结合仍由 `reassociate` 许可与库里的规则表达，匹配器不替它做。
   这是可以观测的：`x*c1 + x*c2 + r` 在 i64 上接住 `a*3 + z + a*5`，在 f64 上原地不动。
7. **重写不丢掉程序跑过的效果**：右式没有读到的匹配项，只有在它是纯计算（无控制输入、无就地写入、
   `carriesOrdering` 为假）时才允许离开程序（设计原则 9.7；重写设计 §8）。`x * 0 => 0` 因此不作用于
   `effectful(a) * 0`——那是"值变了"还是"效果没了"的分界，由引擎统一判定，不交给每条定律自己声明。
   这条检查落地后，H226 的对照语料上出现了一条**可观测的变化**（原先记的是"不变"）：`println(5) * 0`
   上的 `int_mul_zero` 不再触发。保住效应的**既不是**删掉的哪条规则、**也不是** AC 读：`int_mul_zero`
   仍在库里（本分支没删它），`println(5) * 0` 也从来不需要结合交换读；拒绝这次匹配的是引擎的丢弃检查
   （`src/runtime/writings.cpp` 的 `dropsOrdered`/`pureComputation`）。于是
   `algebra_rules.divergence.library_runs` 的期望从四行 `7 9 0 0` 变成五行 `7 9 5 0 0`，与源程序和 C++
   一侧一致；`divergence.differs` 里那条差异也消失，`pairs.mjs` 改为与其余图一样按同构图比较（两条都是
   比"记录差异"更强的守卫：将来有规则或引擎改动再把活效应丢掉，它们会变红）。这正是
   `docs/handover/reviews/questions-2026-10-23.md` 里 `rule-drops-effect` 裁决的 A（检查判给引擎、规则保留，
   提前到 H232 核心）；同一条裁决还要求"把违反 9.7 的四行钉成期望"的写法改掉——本分支的引擎检查已落地，
   五行的正确期望实测通过，所以该条目是绿的、不带 known-fail 标记（运行器的 `known_fail` 只按后端标记
   sweep 条目，普通条目没有这种写法）。
8. **读多重集是一次剪枝过的搜索，不是枚举排列**：先给每个模式项筛出"可能接住"的项（不绑定任何变量），
   有一个筛空了就立刻不匹配；把候选最少的模式项先放；同一个值的两个项可以互换。一条 200 项的和
   （`bench_target.cml` 里的 `main`）曾因枚举排列而卡死（H118 的饱和超时是同一个爆炸），这条约束是修好
   它的原因，也是对后续步骤的约束：任何多重集读法都不许按项数阶乘花钱。
9. **对后续步骤的约束**：任何需要"图上真的规范化"的步骤（按多重集抽取、区域模式、`explain` 的多重集
   说明）都建立在这个读侧视图上；若某一步需要真正的规范节点，那是新的结构决定，另写一页计划，不在这里
   延伸。

## 落地与守卫

已落地（本分支）：

- 匹配器：`src/runtime/writings.cpp` 的 `declaredCommutative` 旁增加 `declaredAssociative`；多重集匹配是
  位置配对与换序之后的第三条路径，只对单向定律（`commuted`）且只在规则根上走（`at == 0`），
  `MatchRemainder` 把"剩余项 + 是哪个变量"交给右式与守卫。
- 键：`DraftWritings::index()` 对 AC 节点的 `NodeKey.norm` 做同一套展平排序（含 `store_.canon`）。
- 效果：`dropsOrdered`/`pureComputation` 在重写与饱和两条路上都拒绝丢掉非纯计算的项。
- 库：`stdlib/rules.cml` 删除 `*_comm`、`*_assoc`、按操作数顺序重复的规则与 `*_one_combine` 孪生规则
  （设计稿 §6；`int32_zero_add`/`float_one_mul`/`tensor_f32_one_combine` 这些也在内），每条定律只写一次。
- 守卫（断言性质，不锁计数）：`test/cases/modules/rules/multiset_{target,counts}.cml` 与
  `rules.plan.toml` 的 `rules.multiset_*`、`rules.store_merges_the_writings_of_one_chain`——`x + 0` 读到
  `a + (b + 0)`；`(x+c1)+c2` 读到常数分开写的三种形状；`x*c1 + x*c2 + r` 读到两种括号法（i64），浮动不
  动（f64）；多重集覆盖不全时定律不触发（`x*c1 + x*c2 + z` 不动）；`effectful(a) * 0` 保留乘积且照旧打印；
  零规则下 `(a+b)+(b+a)` 只剩一个加法、`((a+b)+c)+(a+(b+c))` 只剩一条链，同样两种写法在 f64 上仍是两个值。
- H226 的对照记录跟着上面第 7 条移动（本分支）：`test/plans/feat/opt/algebra_rules.plan.toml` 的
  `algebra_rules.divergence.library_runs`（值）与 `algebra_rules.divergence.differs`（图，`pairs.mjs`）
  从"记录一次 9.7 违规"改成"钉住两侧一致"，语料 `test/cases/opt/algebra_rules/divergence.cml` 保留，
  注释与两条描述都写明机制（引擎拒绝，规则未删）。
- 库的注释按同一条裁决改正（`docs/handover/reviews/questions-2026-10-23.md` 的 `rule-cannot-write-a-tensor`）：
  张量那段"这些律留在 `algebra.cpp`"删掉，改成"浮点零律需要 `finite_math`，写法见规则语言第二版第 4 节"
  （`docs/design/rewrite/v2.cn.md` §4：右边可以调用 Camel 函数）。浮点 `x*0`、`x-x` 不是 0（`NaN*0`、`inf*0`
  是 NaN，负数乘 0 是 `-0`，`inf-inf` 是 NaN），所以浮点零律要 `finite_math`，整数不需要：库里今天没有浮点
  零律与浮点 `x-x`，整数那两条原样不动，因此没有既有规则需要补许可。张量零律的目标拼写
  `t * 0.0 => zeros_like(t)`（`zeros_like` 读 `t` 取形状，所以匹配到的值没被丢掉，丢弃检查也满足）依赖泛型
  规则 `::<T>` 与 `is_float::<T>`，留给第 3 步——第 1 步的泛型规则没有落地（见
  `docs/handover/questions/generic-rule-instantiation.md`）。
- 对照计划的读法按另一条裁决改正：`std::opt::algebra` 与 `rules::simplify` 是同一个东西；库的默认授权就是
  设计原则 7.3 的默认（**允许** `reassociate`/`contract`）；旧表的拒绝（`exclusive()` 的用户规则、`relaxed()`
  不给 `reassociate`）是它自己的偏差，不对齐；规则替换图出口时的锚点交接由引擎做。计划表头与相关条目
  （`divergence.differs`、`library_only.table`、`tensor.table`）按此改写。
- 未做（已知缺口，供协调者决定是否单开一步）：`why_not` 与 `explain` 的措辞仍只走位置配对与换序，一条只在
  多重集上成立的匹配会被 `why_not` 报成"不匹配"；`check` 也不再逐条覆盖交换/结合（那两种性质现在由上面的
  守卫覆盖，而不是由 `<=>` 规则覆盖）。