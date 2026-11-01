# 索引里标"已裁未落"的项，今天在树上到哪一步了

**2026-11-02。** 作者每轮先读 `docs/handover/questions/README.md` 的状态表；表里还挂着一批"已裁未落"，而这一夜有四项已经落地（两个环境变量、`DESIGN-QUESTION`、3.6 措辞与部分故障用例）。这份核对表把"已裁未落"逐条拆成三格：**他的状态 / 树上的状态 / 差在哪一句**。

**核对口径**：HEAD `9a34bd7c0`（`origin/develop` = `bc28c4f3d` 已并）。每条都给了复查命令，**下一轮 develop 变了要重跑命令，不要照抄结论**。

## 一、已经落地（可改"已裁已落"）

| 条目 | 树上的证据 | 建议状态 |
|---|---|---|
| `parallel-map-fault-what-runs-after` | 3.6 已按裁定改措辞（`design-principles.cn.md:199`）；`taskflow.cpp` 里 `DESIGN-QUESTION` **0 命中**；用例 `attempt.partial_fault` 在 `attempt.plan.toml`，`desc` 写明"the four back ends agree" | **已裁已落** |
| `two-references-write-one-object-outside-a-sync-block` 的"②保守写者集合" | `:var/after` 已实现（见下一格），构造在 `builder_expr.cpp:629-631`，`twice` 的验收随 `:var/after` 一起走 | **已裁已落**（只差 `twice` 的一条读数，下一轮补） |
| `a-version-port-that-must-carry-a-value` 的**算子本身** | `OperId VarAfter = 21`、`builtin.cpp:175` kernel、`builtin.cpp:681` `stateOnly`、`builtin_semantics.cpp:343-347` 资源、`builder_expr.cpp:629-631` 链式构造、`builder_diagnostics.cpp:209`；`callback_write_ignored` 四后端打印 `3` | **已裁已落（算子）**，**但 `orderAfterLastWriter` 的 ctrl 替身未拆**——这一条要留"已裁未落"或改成"已裁已落，替身待拆" |
| `generator-cell-type-by-inference` | `FeatureNotSupported("generators")` 在 `src` **0 命中**（`git grep -n 'FeatureNotSupported("generators")' -- src`） | **已裁已落**（若还有别的生成器墙，请以命令复核为准） |
| `who-consumes-the-ruleset-index` 的**索引与规则搬运** | `stdlib/rulesets/jvp.cml`、`transpose.cml` 已在树上（`67b4c17bc`）；`autodiff` 只留包装 | **已裁已落（索引与规则）**；**消费者 `reinterp<jvp>` 未做**——这一条同样是"一半落地" |
| `one-operator-two-names` | `argmax_axis`/`argmin_axis` 只有 `stdlib/tensor.cml:189/196` 一处定义，`array.cml:264` 委托 `tensor` 的 | **已裁已落**（若 ONNX 导出侧还有 `array:`，请以 `git grep -n 'array:argmax' -- src` 复核） |

## 二、确实还没做（保持"已裁未落"）

| 条目 | 树上的状态 | 差在哪 |
|---|---|---|
| `reinterp-rule-set` | `git grep -n 'reinterp' -- stdlib modules` 只中 `reinterpret_cast` | 算子与 `modules/transforms/` 都还没有 |
| `who-consumes-the-ruleset-index` 的**消费者** | 同上 | `reinterp<jvp>` 未实现 |
| `a-version-port-that-must-carry-a-value` 的**替身拆除** | `orderAfterLastWriter`（`builder_expr.cpp:777-796`）仍写 `writeControlLink` | 这一轮正在量"拆了会红谁"，量完给名单 |
| `literal-head-type-expression` | `stdlib/rulesets/jvp.cml:42-56` 的 `zero_tangent` 仍以 `case _ => ()` 收尾 | 元组/结构体臂与字面量头解析未做 |
| `match-type-guard` | 同上一条；`T[..]` 已有 `float[..]` 一族臂，`T[?]` 的任意秩语义未核 | 需一次 `match type` 的实施例复核 |
| `what-carries-a-wait-for-a-write` 的"②a 停造 ctrl" | `writeControlLink` 仍有 15 个调用点（本车道 2026-11-02 的计数） | 停造 ctrl 是片 3b 之后的事 |
| `grad-builds-its-graph-at-runtime` | 依赖 `reinterp`/`split`/`transpose` | 第 6 条未做 |

## 三、要作者答的一句

> **上面第一节的条目在树上已经落地（证据见"树上的证据"一列），我可以把索引状态改成"已裁已落"吗？默认=我改，并在每条后写提交 id；第二节的条目保持"已裁未落"不动。**

**若某一格我判错了，请点名哪一格、以哪条命令为准**——我把命令写进表里就是为此：下一轮谁按命令复跑，都不必相信这份表。

## 四、一处口径提醒

作者的索引写"已裁未落"时看的是 **`develop`**，而我这份核对表看的是**本车道 HEAD**（`9a34bd7c0`，已并 `bc28c4f3d`）。两者之间若有差，**以 `develop` 上的命令输出为准**；我这边的结论只说明"这些改动在本车道已存在"，合并后即等于在 `develop` 上成立。