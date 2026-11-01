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

## 我的判断

**1 能立刻做，3 才是终点。** 但 1 会留下一张"五行符号 + 两条例外"的表，而负责人否认的正是"两处各写一半"（撤回那一笔的理由）。所以先问一句：**要不要直接做 3**，还是先落 1 把 38 条压到 2 条、等标签那一笔再清零？

`等点名`。