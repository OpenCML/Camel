# P1-7 的范围：三张绑定表能删到几行，取决于"不规则的算子怎么点它的规则"

**状态：调查完成，等一个决定；未动代码。** 车道 `lane-gs6`，2026-11-02。

## 一句话

38 条 `bind*Rule` 语句今天摊在三个文件里，其中 **23 条的"键"与算子自己的名字逐字相同**（`tensor:matmul` → `matmul/2`），这部分可以直接推出来、语句删掉；**剩下的 15 条是五个算术族**（`:op/add_d` → `+/2`，模板实例化两次，一条语句出四个键），**再加两条不规则算子**（`:op/idx_arr` → `index_into/2`、`tensor:multiply` → `mul_tensor/2`）。要删到最后一条，得先定"不规则算子怎么点它的规则"——那是语言表面，不是这一车道能自己定的。

## 数出来的现状

| 文件 | 语句 | 生成的键 | 键 = 算子名？ |
|---|---|---|---|
| `modules/tensor/ops/catalog.cpp` `declareVmapRuleKeys` | 20 | 20 | **全部是**（`reshape`、`concat`、`where`、`*_axis` …） |
| `modules/tensor/module.cpp` | 1 | 1 | 否（`tensor:multiply` → `mul_tensor`） |
| `src/builtin/operators/derivative.cpp` | 17 | 32 | 否（`:op/add_d` → `+`，五个族 × 两族/三族） |

`git grep -c 'camel::core::bind[A-Za-z]*Rule(' -- src modules` 给 1/20/17。**38 条语句 → 53 个键**：模板 `registerFloat{Forward,Transpose,Vmap}Rules<F64>` 与 `<F32>` 各实例化一次，五个语句各出两个键。

## 为什么不能"纯粹"从规则集左边建索引

规则集的索引**已经**是按左边建的（`include/camel/parse/ast/rule_head.h#indexRule`：键是左边那个算子的写法 + 操作数个数），`lookupRule(family, head, arity)` 就是查它。缺的一半是**反方向**：手上有一个算子 URI，要问"它左边写成什么"。

- 规则 `x + w` 是**泛型一条**（`T`），实例化到 `float` 是 `:op/add_d`、到 `f32` 是 `:op/add_f`；所以规则那边不能预先按 URI 归档，除非每个数值类型抄一条规则。
- 算子表里**没有"写法"这个字段**（`include/camel/core/operator_table.h` 的条目只有 URI、id、族标志、`CallSpec`）；写法在解析器里是 `BinaryDataOp::Add` 这样的枚举，到编译期才按类型定成具体 URI。
- 一个键只装一条规则（`indexRule` 撞键会报重名），而 `*/2` 已被标量 `:op/mul_d` 占用——所以 `tensor:multiply` 的规则必须换个头（`mul_tensor`），`xs[i]` 更没有 token（三种形状都不是），只能写成调用 `index_into(x, i)`。这两条写在 `reviews/questions-2026-11-01-how-does-an-operator-without-a-token-get-a-key.md` 里。

## 三条可能的做法

1. **按名字推 + 一张五行符号表 + 两个例外**（我推荐的 90% 版）：`ns:name` 的键默认就是 `name`（配算子自己声明的操作数个数），五个算术族按 `add/sub/mul/div/neg` 前缀映射到 `+ - * / -`，`idx_arr` 与 `multiply` 两个例外留在表里。**语句从 38 降到 2**，但 `git grep` 不为零。
2. **给算子声明加"写法"字段**：`OperatorTable::declare` 多收一个 `head`，绑定的键回到算子自己的声明点（"算子在哪声明就在哪绑键"），三张表删掉。语句仍是 38 条，只是从"表"搬到"声明"——`grep` 会归零，但那是把同一个事实换了个存放处。
3. **规则上打标签**（`#rule_for("tensor:multiply")`）：索引真的从规则集左边建起来，算子侧一行都不用写。这是设计 §3 指过的方向（"给函数加标签"），**但它改语言表面**，`vmap` 那条车道也正等着它把 `found=1` 落地。

## 裁定（2026-11-02）

**先做 1，2 是归零的默认路线，3 更贵、由作者再议。**

- **1 现在做**：按名字推 + 一行符号表 + 两个例外，**38 → 2**。但例外不许是一张"代码里躺着、外面看不见"的表：要是**具名的三列**（`URI → 规则名 → 为什么推不出`），理由写清"`xs[i]` 没有 token"、"`*/2` 已被 `:op/mul_d` 占"，**并且这张表要在输出里打印出来**（像 `--ledger-check` 那样）。**例外必须出声。**
- **2 是归零的默认路线**（不必再问）：**算子的声明本来就该说它在源码里长什么样**——每个节点都印成对它的声明的一次调用，写法就是这个声明的一部分。所以"从算子声明里读写法"不是把事实搬到别处，而是把事实放回它该在的地方。
- **3 更贵**：规则上打标签（`#rule_for`）要改语言表面，成本高于 1 和 2，留作者再议。

## 落地（2026-11-02，`lane-gs6`）

做法 1 已经落地，`git grep -c 'camel::core::bind[A-Za-z]*Rule(' -- src modules` 从 **38 → 0**（比裁定的 2 还少：两个例外不是"绑一次"，而是具名表的一项，被 `ruleKeyOf` 先查，所以一行绑定语句都不剩）。

- 键**从算子自己推**：`:op/add_d` 就是 `+` / 2，因为 `add` 的意思如此；`tensor:sum_axis` 就是 `sum_axis` / 3，因为算子的名字就是它的写法，而个数由族索引回答（新增 `camel::parse::ast::arityOfHead(family, head)`：该族里这个名字恰好只有一条规则时回答它的操作数个数，多条就拒绝猜）。
- 两个例外在 `include/camel/core/forward.h#RuleKeyException` 里是**具名三列**：`uri`、`head`、`why`（外加 `arity`）。`ruleKeyExceptions()` 是这张表，`ruleKeyExceptionLines()` 是它的打印形式。
- **表在输出里**：`camel <case> std::rulesets forward=true` 每次都打印
  `RULEKEY-EXCEPTION :op/idx_arr -> index_into/2 because …` 与 `RULEKEY-EXCEPTION tensor:multiply -> mul_tensor/2 because …`，
  再加一句 `RULEKEY derivable: …` 说明其余的键是怎么来的。例外不静默。
- 三道读法一起改：`forwardRuleOf` / `transposeRuleOf` / `vmapRuleOf` 都走 `ruleKeyOf(family, uri)`；三张登记表、`ForwardRuleBinding`、`bindForwardRule`/`bindTransposeRule`/`bindVmapRule`、`forwardBindingOf`/`vmapBindingOf` 全部删除；`modules/tensor/ops/catalog.cpp#declareVmapRuleKeys` 与 `modules/tensor/module.cpp` 的 21 条绑定随之清空。
- 对外可观测的行为没变：`feat.compile.rulesets` 63/63，整条 `fast` 闸门 `total: 8353, pass: 8341, fail: 0`；`RULESET`/`LOOKUP` 两张读法一字未动，`rulesets_lookup.py` 现在把"两个例外 + 各自的键解析到哪条规则"当作它盯的那一半。

## 裁定（2026-11-02，之后）

作者另裁，三条都不选：**键本来就有唯一的来源——把算子绑到声明上的那一行**
（`#op(':op/add_d') export func operator +(x: float, y: float): float` 就能读出头 `+`、元数 2）。
做法 ②（算子声明带写法字段）因此被否，落地的一半已撤回；`ruleKeyExceptions()` 的那张表也随第二步整张删掉。

顺序是**依赖**，不能跳：

1. **P1-13**：让每个算子都有一条被编译的 `#op` 声明（先量，不动键）；
2. **键从声明读**：建索引时扫 `#op` 声明得到 `(uri → head, arity)`，删三张 `bind*Rule`、`forward.cpp` 的三份 map、`*Descriptions`、`ruleKeyOf`、`RuleKeyException`；
3. **同键多规则按类型选**（像重载）：标量 `*` 与张量 `*` 都叫 `*`/2，**`mul_tensor` / `vmap_mul_tensor` 这个名字删掉**；`indexRule` 撞键改成"同键同类型条件才报"；
4. **`operator []`**：前奏加 `:op/idx_arr` 的运算符声明 + 解析器支持 + 规则左边写 `xs[i]` + **删 `index_into`**。

## 前置读数（P1-13 第 1 步）

585 个注册算子里 **347** 个没有任何被编译的 `#op` 声明；五个算术族的 34 个里只有 4 个（`+`、`-` 的 d/f），
**`*`、`/`、一元 `-` 一条键都读不出来**；`tensor` 协议 86 个算子 **0 条**被编译声明。
全文与 347 行清单见 [2026-11-02-p1-13-which-operator-uris-have-no-compiled-declaration.cn.md](../reports/2026-11-02-p1-13-which-operator-uris-have-no-compiled-declaration.cn.md)。

`等点名`。
