# 作者一次答完 48 页：逐条落点

底：`origin/fsh-dev` = `61691282a`（`origin/develop` = `d7f15f2b3` + 10 笔），本车道 `lane-fsh1` 以快进方式站到它上面。
裁定原文：`docs/handover/reviews/questions-2026-10-29.md` 的 `## 裁定（2026-11-04 04:20）：48 页一次答完`，七个小节。
每行：**页名 / 裁定 / 落点（`file#Symbol` 或车道）/ 现在是否已在树上**。状态三类：**已落**（附我核过的提交）、**待做**、**相反**（树上今天写的正是它要删或要改的那一侧 ⇒ 要改代码，见最后一节）。

## 一、照默认做

| 页名 | 裁定 | 落点 | 已在树上 |
| --- | --- | --- | --- |
| `does-pick-at-need-a-forward-rule` | 需要一条前向律 | `stdlib/rulesets/jvp.cml#pick_at` | 已落 `21e3cb340` |
| `does-the-engine-need-a-pick-at-rule` | 引擎不另加一条 | `modules/tensor/ops/layout_ops.cpp#pick_at` | 已落（树上已有，无新增） |
| `does-the-transpose-walk-still-refuse-by-arity` | 照默认：仍按元数拒绝 | `modules/transforms/transpose.cpp#buildTransposeMember` | 树上即如此，无改动 |
| `how-does-a-tie-mask-travel-with-an-extremums-shape` | 掩码是已知操作数，走 `lin` 的闭包，不加端口 | `stdlib/rulesets/jvp.cml` 的 `max_axis_stated`/`min_axis_stated` | 已落 `3d95855c7` |
| `narrow-the-declaration-or-teach-the-consumer` | 照默认 | — | 无改动 |
| `should-an-owner-name-be-checked-against-a-roster` | 照默认 | — | 无改动 |
| `should-the-compiler-version-invalidate-an-install` | 照默认 | — | 无改动 |
| `the-two-tuple-cases-have-no-closure-member` | 闭包成员不收余切，单独一步落 | `modules/transforms/transpose.cpp#wantsCotangent` | 已落 `bbfc003ec` |
| `what-answers-a-length-from-a-type-alone` | `extent_of` 已在树上，结案 | `extent_of::<A>()` | 已落 `7ea644415` |
| `what-does-a-call-of-a-value-that-is-not-a-function-say` | 照默认 | — | 无改动 |
| `where-the-seal-says-the-exit-type-is-wrong` | 确认它就是 `commit` 时那条出口类型校验，不是第二份 | 引擎的出口类型校验 | 待核（本页未独立读） |
| `widening-the-float-guard-turns-ten-hardcoded-types-into-defects` | 照默认 | — | 无改动 |
| `a-struct-field-read-refused-or-copied-by-key` | 已落，结案 | 成员读的读法 | 作者已结案 |
| `axis-reductions-state-their-extents` | `array.cml` 的六个函数同一轮一起改 | `stdlib/array.cml` | **待做**：`array.cml` 今天没有一个 `*_stated` 函数（`tensor.cml` 有 21 处） |
| `does-a-call-stand-on-its-callees-lin-or-on-its-body` | 等 `residualsOf` 那条车道落了再回来，按 17:55 改成调用 `lin` | `modules/transforms/split.cpp#residualsOf` | **待做**：`residualsOf` 已在树上，等车道 |
| `does-a-build-gated-case-count-as-a-name-pin` | 诊断名覆盖率只当信息，不当通过条件 | 判据 | 树上没有这条判据，无需改 |
| `a-member-read-is-not-a-parameter-read` | 甲：参数成员的读取把余切累加到参数上；先修那个元组的段错误 | `modules/transforms/transpose.cpp`（成员读的余切） | **待做** |

## 二、零载体与形状：照 R3/R11

| 页名 | 裁定 | 落点 | 已在树上 |
| --- | --- | --- | --- |
| `does-a-product-transpose-still-need-a-zero-carrier` | 现在就删掉 `shapeFromTypeOf` 里的零载体回落 | `modules/transforms/transpose.cpp#shapeFromTypeOf` | **相反**：回落还在，调用点照旧造载体 |
| `which-shape-reads-can-become-extent-of` | 写死形状的类型用 `extent_of`；开秩的裸 `transpose` 按语言规则拒绝 | `extent_of`、`#shapeFromTypeOf` | 前半已落 `7ea644415`，后半同上待改 |
| `who-states-the-extents-a-product-law-reads` | 由 `jvp` 规则把长度作为已知实参交出，需要一个收长度的 `sum_to` | `stdlib/rulesets/jvp.cml`、`stdlib/tensor.cml#sum_to` | **待做** |
| `who-states-the-length-when-the-shape-is-open` | `sum_to(t, zeros::<T>(at))` 用已知长度造模板，规则上可接受；更干净的是收 `at` 的算子，排后面 | `stdlib/tensor.cml#sum_to` | 待做（"更干净的那个"排在后面） |
| `should-a-multiplys-adjoint-need-its-operands-shape` | 不取"开秩上拒绝"：`exp`/`erf`/`gelu` 的 `jvp` 把形状作为已知实参交给乘法 | `stdlib/rulesets/jvp.cml` | **待做** |
| `is-an-open-rank-zeros-answer-an-empty-array-or-a-refusal` | `[]` 是长度不对的零，不取；`vjp` 路径照原值形状造，原值是残差、在闭包里 | `stdlib/rulesets/jvp.cml` | **待做** |
| `what-shape-a-zero-with-no-stated-extent-has` | 同上；裸 `transpose` 在开秩参数上拒绝 | 同上 | **待做** |

## 三、秩 0：只有一个转换点

| 页名 | 裁定 | 落点 | 已在树上 |
| --- | --- | --- | --- |
| `a-rank-zero-cotangent-is-read-as-a-pointer` | 规则的端口照实写 `T[..]`，不把每条 `*_stated` 规则都约定成写 `ct: T` | `stdlib/rulesets/transpose.cml#mean_axis_stated_t` | **相反**：`8fa4f742a` 刚把它从 `float[..]` 改成 `(ct: T)` |
| `who-gives-a-shapeless-cotangent-its-rank` | 标量进 `T[..]` 位置由编译器在可赋值转换那一处统一转换，不在 `ruleCotangent` 或各条规则里各补一次 | `modules/transforms/transpose.cpp#ruleCotangent`、`Builder::coerceTo` | **相反**：`8fa4f742a` 试过改 `ruleCotangent`（读数 inert），改的是端口 |
| `who-states-the-extents-of-a-stated-reduction` | 端口照实写 `T[..]`；`numel`/`shape` 对秩 0 答 `1`/`[]`；用例里为绕开问题写的 `keepdims` 改回来 | `stdlib/rulesets/transpose.cml`、`stdlib/tensor.cml#numel` | **相反 + 待做** |
| 连带：lint 的路 3 收窄 | 端口照实写以后，"秩 0 等价"不再是要放过的那类 | `scripts/law-cotangent-ports.mjs#isRankZeroEquivalence` | **相反**：这条路今天正是为 `(ct: T)` 那一侧开的 |

## 四、不加端口、不加出处：照 R2/R4

| 页名 | 裁定 | 落点 | 已在树上 |
| --- | --- | --- | --- |
| `how-is-a-two-port-transposed-value-called` | 不加值端口；要用 `x` 的走 `vjp`（`x` 是 `lin` 闭包里的残差）；裸 `transpose(read_outer)` 照 16:15 拒绝 | `modules/transforms/transpose.cpp#buildTransposeMember` | 树上没有值端口，无改动 |
| `does-a-transposed-member-get-a-port-for-a-parameter-value` | 同上；`Transpose<F>` 的声明照 22:30 写成 `(ct) => …`，声明和构建一致 | `modules/transforms/transpose.cpp#transposeSignature` | 已落：签名就是"收余切、不带参数" |
| `when-the-lift-promotes-a-with-value-and-the-cut-cannot-take-the-port` | `with` 值不进抬升后的值列表，照 15:15 作为残差进闭包；删掉"只提升标量 `with`"那道门和那句诊断 | `modules/transforms/jvp_operator.cpp` 的 `does not reinterpret … yet` | **相反**：那句诊断还在树上 |
| `who-reads-the-first-operand-as-a-callee` | 删掉那段读法是对的；剩下的 `mk(x)(2.0)` 按内容处理，转置成调用 `transpose(v)`，现在就做 | `modules/transforms/transpose.cpp` | **待做** |
| `can-a-pass-build-the-graph-a-transform-hands-out` | 引擎里"这是 `transforms:split` 的结果"那句拒绝要删，让库层 `grad` 覆盖这些形状 | 引擎（按出处判断处） | **待做** |
| `where-a-laws-port-disagrees-with-the-value-it-is-handed` | 除静态检查外，在挑规则的共用函数里也比对端口类型与交来的值类型，对不上按语言规则拒绝；lint 照留 | `modules/transforms/transpose.cpp`（挑规则处） | **待做** |

## 五、`with` 值与库层 `grad`

| 页名 | 裁定 | 落点 | 已在树上 |
| --- | --- | --- | --- |
| `does-a-with-value-get-a-tangent` | 不取甲：`with` 值在变换层是常量，第一个 `with` 参数由 `grad` 用 lambda 挪到普通参数；顺带两个缺陷各自修 | `stdlib/rulesets/jvp.cml`（`zeros_like`）、`jvp` 的实参顺序 | 缺陷一已落 `c8bf5b4de`；缺陷二待核 |
| `should-the-library-with-grad-be-an-export-that-refuses` | 删掉那个永远拒绝的导出：永远拒绝的公开函数就是"暂不支持"；`with_grad` 只留引擎那一份 | `stdlib/vjp.cml#with_grad`，连带用例 `test/cases/compile/transforms/with_grad_from_the_library.cml` 与 `test/plans/feat/compile/transforms.plan.toml` 的 `transforms.with_grad_from_the_library` | **相反**：树上有这份导出，返回 `never` 并带一句拒绝话 |
| `should-the-library-grad-be-declared-per-arity` | 不按元数分写。**裁定已到**（`d05035fed`，规章层）：用类型层拆包在标准库里写 `Gradient<F>`，不再由 C++ 另算一份 | `stdlib/types.cml`、`stdlib/signatures/autodiff.cml` | **可开工**：树上仍由 C++ 算（`modules/autodiff/operators.cpp#GradResolver`、`#gradientSignature`），`stdlib/types.cml` 里没有 `Gradient<F>` |
| `should-the-library-grow-a-has-aux-form` | 库层 `grad`/`with_grad` 写成组合时一并带 `has_aux`。**裁定已到**（`d05035fed`）：两个 `…AndAux<F>` 同样用类型层拆包写 | `stdlib/types.cml`、`stdlib/autodiff.cml` | 部分已落 `a6471ef3c`（`has_aux` 形已在）；`…AndAux<F>` 的声明**可开工** |
| `does-the-library-need-forward-rules-for-the-stated-spellings` | 取甲：四个都是线性的，每条规则一行 | `stdlib/rulesets/jvp.cml` | **待做** |

## 六、规则与判据

| 页名 | 裁定 | 落点 | 已在树上 |
| --- | --- | --- | --- |
| `a-guarded-tangent-cannot-be-transposed-what-does-the-power-keep` | 零切向量是**结构上**已知的，不是按值判断；`pow_tangent` 去掉 `dw == 0` 这个按值守卫，改由规则展开时省略结构上为零的那一项；`^` 与 `pow` 答同样的数 | `stdlib/rulesets/jvp.cml#pow_tangent` | **相反**：守卫还在（`if dw == T(0) then T(0) else …`） |
| `does-a-transpose-law-count-when-the-first-order-walk-does-not-read-it` | `abs_t`/`log_t`/`maximum_t`/`minimum_t` 各写一条能分辨对错的用例；写之前列"未验证" | 用例 | **待做** |
| `two-f32-combination-cases-meet-their-own-call-text` | 取 A，删掉旧条目（旧条目钉的是错的调用写法） | 用例与 plan | 本树未找到该旧条目，待核是否已删 |
| `a-parameter-holds-no-cotangent-on-a-two-operand-reduction` | 照默认留已知失败标记，但先查"参数没被用到"的判据是不是把闭包里的值当成了没用到 | 判据 | **待做**（查判据） |
| `a-draft-node-asked-of-the-source-graph` | 直接做 C：改成收 `Type*`，删掉悄悄回落成 `float` 的那一支 | `modules/transforms/transpose.cpp#buildTransposeMember` | **待做** |
| `a-case-with-no-plan-entry-and-a-main` | 不留在"不算通过、也不算失败"：查它为什么退出 1，是拒绝就钉诊断码，是坏了就删 | 用例 | **已按裁定落地 `32123266a`**：补了正例行，钉 `12.0` / `18.0` |
| `when-a-lookup-that-finds-nothing-should-refuse` | 照默认三分法；"对数组循环的元素切向"若合法就是缺口、排进去做，拒绝就写成语言规则 | — | 无改动 |
| `what-does-the-second-derivative-need` | 照默认；函数值要不要有导数类型是以后的语义问题，现在不开 | — | 无改动 |

## 七、图的归属

| 页名 | 裁定 | 落点 | 已在树上 |
| --- | --- | --- | --- |
| `does-adopt-carry-what-it-adopted-or-does-a-cut-carry-its-own-chain` | 只要一种机制：程序传递地收养嵌套切出来的图，按 id 去重；删掉每个切口各自带链的做法 | `modules/transforms/split.h#SplitGraph`（`standing`）、`modules/transforms/split.cpp` | **相反**：`standing` 那条链今天在树上 |
| `who-adopts-the-graphs-of-a-nested-cut` | 同上；`calleeCuts` 每次变换调用只建一张表，不随嵌套层数翻倍 | `modules/transforms/split.cpp#calleeCuts` | **相反→待改**；`lane-batch3` 的 `9262df328`（"切只命名直接 callee，其余靠收养"）正在做这件事，尚未进 develop |

## 规章层的变化（规范性的）

`d05035fed`（11-04 04:12）在 `docs/design-principles.cn.md` 与 `.en.md` 的 §5 加了一条，紧接在"参数包是同一条规则"之后：

> **类型层的包同样能拆**：值层能写 `<w, ..rest>`，类型层也能把一个类型包拆成"第一个成员"和"其余"，例如对 `WithOf<F>` 拆出第一个 `with` 参数的类型 `W`。由参数算出的函数类型（`Gradient<F>`、`ValueAndGradient<F>` 等）都用这种写法在标准库里声明，不由 C++ 另算一份。空包拆不出第一个成员，在实例化处按语言规则报错。

同一笔在 `docs/handover/reviews/questions-2026-10-29.md` 的 `## 负责人定（2026-11-04 04:30）：类型层的包能拆` 里给了施工四步（下一批第 3 项后半）：

| 步骤 | 做什么 | 落点 | 状态 |
| --- | --- | --- | --- |
| ① | 类型位置的拆包写法，具体语法由施工方提一页（`match type` 的元组模式，或类型位置上的模式），与值层 `<w, ..rest>` 读起来一致；空包在实例化处按语言规则报错 | 一页新问题页 + 语法 | 待做（先提页） |
| ② | 用它写 `Gradient<F>`、`ValueAndGradient<F>` 与两个 `…AndAux<F>`；`stdlib/signatures/autodiff.cml` 照这几个声明 | `stdlib/types.cml`、`stdlib/signatures/autodiff.cml` | 待做 |
| ③ | 库层 `grad`/`with_grad`/`has_aux` 写成组合（原则 9.9：lambda 把第一个 `with` 参数挪到普通参数位，再 `vjp`），与引擎在全部 autodiff 用例上逐元素同答 | `stdlib/autodiff.cml`、`stdlib/vjp.cml` | 待做 |
| ④ | 之后删 C++ 的 `GradResolver`/`gradientSignature`、`pullback.cpp`、`buildGradientGraph`（第 7 项） | `modules/autodiff/operators.cpp#GradResolver` | 待做（在 ①②③ 之后） |

**它解开了两个原本"等作者"的待办**（第五节对应的两行已改成"可开工"）：

- `should-the-library-grad-be-declared-per-arity`：不再"等类型层拆包的裁定"，改成"按类型层拆包写"。`stdlib/types.cml` 今天没有 `Gradient<F>`，类型是 `modules/autodiff/operators.cpp#GradResolver` 与 `#gradientSignature` 另算的一份——正是裁定要停掉的那种"C++ 另算一份"。
- `should-the-library-grow-a-has-aux-form`：不再"`vjp` 的 `has_aux` 随后再定"，改成"用 `…AndAux<F>` 写"。`stdlib/signatures/autodiff.cml` 已有 `GradientAndAux<F>` 与 `ValueAndGradientAndAux<F>` 两个名字（`a6471ef3c` 给了 `has_aux` 形），但它们的类型仍由 C++ 算；第 ② 步把这两个名字挪进 `stdlib/types.cml` 用拆包写。

## 要改代码的（与本树现状相反）

1. **三**：端口照实写 `T[..]`。本树 `stdlib/rulesets/transpose.cml#mean_axis_stated_t` 今天写的是 `(ct: T)`，那是 `8fa4f742a`（11-04 07:10）刚做的、并且让 8 条段错误用例转绿的改动；`scripts/law-cotangent-ports.mjs#isRankZeroEquivalence` 是同一轮为它开的"路 3 收窄"。照裁定要反过来：端口写 `T[..]`，由编译器在可赋值转换那一处统一做表示转换。**`8fa4f742a` 的消息里已量到改 `ruleCotangent` 是 inert，所以这次要改的是"转换点"，不是"端口"**——两者不一致，需要一条专门的车道。
2. **五**：`stdlib/vjp.cml#with_grad` 那份永远拒绝的导出删掉，连它的用例、plan 行与 `stdlib/signatures` 里的声明一起。
3. **六**：`stdlib/rulesets/jvp.cml#pow_tangent` 的 `dw == 0` 守卫删掉，改成规则展开时省略结构上为零的那一项。
4. **四**：`modules/transforms/jvp_operator.cpp` 的 `does not reinterpret … yet` 诊断与"只提升标量 `with`"那道门一起删。
5. **七**：`modules/transforms/split.h#SplitGraph` 的 `standing` 链删掉，收养改由程序传递地按 id 去重（`lane-batch3` 已在做）。
6. **二**：`modules/transforms/transpose.cpp#shapeFromTypeOf` 的零载体回落删掉。

48 页挪到 `reviews/` 留档，`docs/handover/questions/` 现在**空**（0 页），`README.md` 已由生成器重写为"目录为空"。"类型层拆包"这件要负责人定的已在 `d05035fed` 到了（见上一节）；本批之外没有新增要负责人定的事。有一处定位假设我们照做了、打印出来指向别处，单独写了一页：`docs/handover/questions/2026-11-04-the-refusal-was-not-a-closure-member.cn.md`。