# 含"洞"的复合字面量在**参数位置**与**声明位置**都没取到期望类型（箭头与普通调用逐字同一条诊断）

这条不是"箭头"的问题：车道 `h-arrowlit` 按你的裁定去验"复合字面量写在 `->` 下面有没有取到形参类型"，结果**那个形状已经绿了**（`[1.0, 2.0]->total`，`total(xs: f32[])`，在函数体里、`main` 里、以及结构体/元组/嵌套数组/`[]`/链六种同位形状都无诊断；修复是已进 develop 的 `52ced85a8`/`5e1fe6e01` 里那句 "a literal takes its type through an arrow"）。它顺手把**真正剩下的缺口**复现出来了，我替它报你定：

## 逐字读数（主机，`-E short`／同一函数内并列）
- 全字面量（绿）：`func a1(): f32 { return [1.0, 2.0]->total }` → **PASS**
- 含一个**计算元素**的字面量（红，箭头接收者）：`[1.0, float(n)]->total` → `No matching function found for argument types: <> (float[]). total: (xs: f32[]) => f32 pure`
- **同一写法的普通实参，逐字同一条诊断**：`total([1.0, float(n)])` → 同句（只有行号不同）
- **声明位置也红**：`let xs: f32[] = [1.0, float(n)]` → `ElementTypeMismatch: Cannot insert data of type 'float' into a(n) array with element type 'f32'`
- **反例（绿）**：同一写法的**结构体字段/元组字段**没问题（`{ n: n, v: [1.0, 2.0] }` 通过，`context_composite_holes` 的四种嵌套也绿）

## 根因（车道读代码得出）
`compositeEntersIn`／`compositeLiteralEntersIn` 只认 `CompositeData::resolved()`（**已定型的**复合字面量）；带洞的字面量走 FILL 分支后，**形参类型从不进实参**。`eff0280d9` 只补了"**声明位置**读 `expectedValueType_`"那一半，它自己的提交信息里就写着"**the parameter position that has the same gap**"，并把它挂在 §15 的未决条目 **`entering-operator-args`** 上（那页问题稿已被 develop 删掉，理由留在 `eff0280d9` 的提交信息与 `391d5913b` 一带）。

## 请你定
1. **"含洞的复合字面量按期望类型定型"是不是你要的语义**（洞的期望类型沿"参数位置/声明位置"传进元素——这看起来正是 §15 `entering-operator-args` 那条未决项的答案）？
2. 若是：**实现**就动 `compositeEntersIn`（可复用现成的 `compositeWithHolesInType`）＋声明位置的数组分支 —— 这是车道要做的活，等你一句话；现在**一行代码都没动**。
3. 若你要的是"洞的期望类型只在声明位置传、参数位置不传"，也请明说，我们据此把用例与诊断保持一致。

（本条与"内建声明在哪里编译"那条无关，是另一条独立缺口。）